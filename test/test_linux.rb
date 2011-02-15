# -*- encoding: binary -*-
require 'test/unit'
require 'tempfile'
require 'raindrops'
require 'socket'
require 'pp'
$stderr.sync = $stdout.sync = true

class TestLinux < Test::Unit::TestCase
  include Raindrops::Linux

  TEST_ADDR = ENV['UNICORN_TEST_ADDR'] || '127.0.0.1'

  def test_unix
    tmp = Tempfile.new("\xde\xad\xbe\xef") # valid path, really :)
    File.unlink(tmp.path)
    us = UNIXServer.new(tmp.path)
    stats = unix_listener_stats([tmp.path])
    assert_equal 1, stats.size
    assert_equal 0, stats[tmp.path].active
    assert_equal 0, stats[tmp.path].queued

    uc0 = UNIXSocket.new(tmp.path)
    stats = unix_listener_stats([tmp.path])
    assert_equal 1, stats.size
    assert_equal 0, stats[tmp.path].active
    assert_equal 1, stats[tmp.path].queued

    uc1 = UNIXSocket.new(tmp.path)
    stats = unix_listener_stats([tmp.path])
    assert_equal 1, stats.size
    assert_equal 0, stats[tmp.path].active
    assert_equal 2, stats[tmp.path].queued

    ua0 = us.accept
    stats = unix_listener_stats([tmp.path])
    assert_equal 1, stats.size
    assert_equal 1, stats[tmp.path].active
    assert_equal 1, stats[tmp.path].queued
  end

  def test_tcp
    port = unused_port
    s = TCPServer.new(TEST_ADDR, port)
    addr = "#{TEST_ADDR}:#{port}"
    addrs = [ addr ]
    stats = tcp_listener_stats(addrs)
    assert_equal 1, stats.size
    assert_equal 0, stats[addr].queued
    assert_equal 0, stats[addr].active

    c = TCPSocket.new(TEST_ADDR, port)
    stats = tcp_listener_stats(addrs)
    assert_equal 1, stats.size
    assert_equal 1, stats[addr].queued
    assert_equal 0, stats[addr].active

    sc = s.accept
    stats = tcp_listener_stats(addrs)
    assert_equal 1, stats.size
    assert_equal 0, stats[addr].queued
    assert_equal 1, stats[addr].active
  end

  def test_tcp_multi
    port1, port2 = unused_port, unused_port
    s1 = TCPServer.new(TEST_ADDR, port1)
    s2 = TCPServer.new(TEST_ADDR, port2)
    addr1, addr2 = "#{TEST_ADDR}:#{port1}", "#{TEST_ADDR}:#{port2}"
    addrs = [ addr1, addr2 ]
    stats = tcp_listener_stats(addrs)
    assert_equal 2, stats.size
    assert_equal 0, stats[addr1].queued
    assert_equal 0, stats[addr1].active
    assert_equal 0, stats[addr2].queued
    assert_equal 0, stats[addr2].active

    c1 = TCPSocket.new(TEST_ADDR, port1)
    stats = tcp_listener_stats(addrs)
    assert_equal 2, stats.size
    assert_equal 1, stats[addr1].queued
    assert_equal 0, stats[addr1].active
    assert_equal 0, stats[addr2].queued
    assert_equal 0, stats[addr2].active

    sc1 = s1.accept
    stats = tcp_listener_stats(addrs)
    assert_equal 2, stats.size
    assert_equal 0, stats[addr1].queued
    assert_equal 1, stats[addr1].active
    assert_equal 0, stats[addr2].queued
    assert_equal 0, stats[addr2].active

    c2 = TCPSocket.new(TEST_ADDR, port2)
    stats = tcp_listener_stats(addrs)
    assert_equal 2, stats.size
    assert_equal 0, stats[addr1].queued
    assert_equal 1, stats[addr1].active
    assert_equal 1, stats[addr2].queued
    assert_equal 0, stats[addr2].active

    c3 = TCPSocket.new(TEST_ADDR, port2)
    stats = tcp_listener_stats(addrs)
    assert_equal 2, stats.size
    assert_equal 0, stats[addr1].queued
    assert_equal 1, stats[addr1].active
    assert_equal 2, stats[addr2].queued
    assert_equal 0, stats[addr2].active

    sc2 = s2.accept
    stats = tcp_listener_stats(addrs)
    assert_equal 2, stats.size
    assert_equal 0, stats[addr1].queued
    assert_equal 1, stats[addr1].active
    assert_equal 1, stats[addr2].queued
    assert_equal 1, stats[addr2].active

    sc1.close
    stats = tcp_listener_stats(addrs)
    assert_equal 0, stats[addr1].queued
    assert_equal 0, stats[addr1].active
    assert_equal 1, stats[addr2].queued
    assert_equal 1, stats[addr2].active
  end

  # tries to overflow buffers
  def test_tcp_stress_test
    nr_proc = 32
    nr_sock = 500
    port = unused_port
    addr = "#{TEST_ADDR}:#{port}"
    addrs = [ addr ]
    s = TCPServer.new(TEST_ADDR, port)
    rda, wra = IO.pipe
    rdb, wrb = IO.pipe

    nr_proc.times do
      fork do
        rda.close
        wrb.close
        socks = (1..nr_sock).map { s.accept }
        wra.syswrite('.')
        wra.close
        rdb.sysread(1) # wait for parent to nuke us
      end
    end

    nr_proc.times do
      fork do
        rda.close
        wrb.close
        socks = (1..nr_sock).map { TCPSocket.new(TEST_ADDR, port) }
        wra.syswrite('.')
        wra.close
        rdb.sysread(1) # wait for parent to nuke us
      end
    end

    assert_equal('.' * (nr_proc * 2), rda.read(nr_proc * 2))

    rda.close
    stats = tcp_listener_stats(addrs)
    expect = { addr => Raindrops::ListenStats[nr_sock * nr_proc, 0] }
    assert_equal expect, stats

    uno_mas = TCPSocket.new(TEST_ADDR, port)
    stats = tcp_listener_stats(addrs)
    expect = { addr => Raindrops::ListenStats[nr_sock * nr_proc, 1] }
    assert_equal expect, stats

    if ENV["BENCHMARK"].to_i != 0
      require 'benchmark'
      puts(Benchmark.measure{1000.times { tcp_listener_stats(addrs) }})
    end

    wrb.syswrite('.' * (nr_proc * 2)) # broadcast a wakeup
    statuses = Process.waitall
    statuses.each { |(pid,status)| assert status.success?, status.inspect }
  end if ENV["STRESS"].to_i != 0

private

  # Stolen from Unicorn, also a version of this is used by the Rainbows!
  # test suite.
  # unused_port provides an unused port on +addr+ usable for TCP that is
  # guaranteed to be unused across all compatible tests on that system.  It
  # prevents race conditions by using a lock file other tests
  # will see.  This is required if you perform several builds in parallel
  # with a continuous integration system or run tests in parallel via
  # gmake.  This is NOT guaranteed to be race-free if you run other
  # systems that bind to random ports for testing (but the window
  # for a race condition is very small).  You may also set UNICORN_TEST_ADDR
  # to override the default test address (127.0.0.1).
  def unused_port(addr = TEST_ADDR)
    retries = 100
    base = 5000
    port = sock = nil
    begin
      begin
        port = base + rand(32768 - base)
        while port == 8080
          port = base + rand(32768 - base)
        end

        sock = Socket.new(Socket::AF_INET, Socket::SOCK_STREAM, 0)
        sock.bind(Socket.pack_sockaddr_in(port, addr))
        sock.listen(5)
      rescue Errno::EADDRINUSE, Errno::EACCES
        sock.close rescue nil
        retry if (retries -= 1) >= 0
      end

      # since we'll end up closing the random port we just got, there's a race
      # condition could allow the random port we just chose to reselect itself
      # when running tests in parallel with gmake.  Create a lock file while
      # we have the port here to ensure that does not happen .
      lock_path = "#{Dir::tmpdir}/unicorn_test.#{addr}:#{port}.lock"
      lock = File.open(lock_path, File::WRONLY|File::CREAT|File::EXCL, 0600)
      at_exit { File.unlink(lock_path) rescue nil }
    rescue Errno::EEXIST
      sock.close rescue nil
      retry
    end
    sock.close rescue nil
    port
  end

end if RUBY_PLATFORM =~ /linux/
