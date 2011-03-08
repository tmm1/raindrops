# -*- encoding: binary -*-
require "tempfile"
require "aggregate"
require "posix_mq"
require "fcntl"
require "io/extra"

# Aggregate + POSIX message queues support
class Raindrops::Aggregate::PMQ
  RDLOCK = [ Fcntl::F_RDLCK ].pack("s @256")
  WRLOCK = [ Fcntl::F_WRLCK ].pack("s @256")
  UNLOCK = [ Fcntl::F_UNLCK ].pack("s @256")

  attr_reader :nr_dropped

  def initialize(params = {})
    opts = {
      :queue => ENV["RAINDROPS_MQUEUE"] || "/raindrops",
      :worker_interval => 10,
      :master_interval => 5,
      :lossy => false,
      :mq_attr => nil,
      :mq_umask => 0666,
      :aggregate => Aggregate.new,
    }.merge! params
    @master_interval = opts[:master_interval]
    @worker_interval = opts[:worker_interval]
    @aggregate = opts[:aggregate]
    @worker_queue = @worker_interval ? [] : nil

    @mq_name = opts[:queue]
    mq = POSIX_MQ.new @mq_name, :w, opts[:mq_umask], opts[:mq_attr]
    Tempfile.open("raindrops_pmq") do |t|
      @wr = File.open(t.path, "wb")
      @rd = File.open(t.path, "rb")
    end
    @cached_aggregate = @aggregate
    flush_master
    @mq_send = if opts[:lossy]
      @nr_dropped = 0
      mq.nonblock = true
      mq.method :trysend
    else
      mq.method :send
    end
  end

  def << val
    if q = @worker_queue
      q << val
      if q.size >= @worker_interval
        mq_send(q) or @nr_dropped += 1
        q.clear
      end
    else
      mq_send(val) or @nr_dropped += 1
    end
  end

  def mq_send(val)
    @cached_aggregate = nil
    @mq_send.call Marshal.dump(val)
  end

  def master_loop
    buf = ""
    a = @aggregate
    nr = 0
    mq = POSIX_MQ.new @mq_name, :r # this one is always blocking
    begin
      if (nr -= 1) < 0
        nr = @master_interval
        flush_master
      end
      mq.shift(buf)
      data = begin
        Marshal.load(buf) or return
      rescue ArgumentError, TypeError
        next
      end
      Array === data ? data.each { |x| a << x } : a << data
    rescue Errno::EINTR
    rescue => e
      warn "Unhandled exception in #{__FILE__}:#{__LINE__}: #{e}"
      break
    end while true
    ensure
      flush_master
  end

  def aggregate
    @cached_aggregate ||= begin
      flush
      Marshal.load(synchronize(@rd, RDLOCK) do |rd|
        IO.pread rd.fileno, rd.stat.size, 0
      end)
    end
  end

  def flush_master
    dump = Marshal.dump @aggregate
    synchronize(@wr, WRLOCK) do |wr|
      wr.truncate 0
      IO.pwrite wr.fileno, dump, 0
    end
  end

  def stop_master_loop
    sleep 0.1 until mq_send(false)
    rescue Errno::EINTR
      retry
  end

  def lock! io, type
    io.fcntl Fcntl::F_SETLKW, type
    rescue Errno::EINTR
      retry
  end

  def synchronize io, type
    lock! io, type
    yield io
    ensure
      lock! io, UNLOCK
  end

  def flush
    if q = @local_queue && ! q.empty?
      mq_send q
      q.clear
    end
    nil
  end

  def count; aggregate.count; end
  def max; aggregate.max; end
  def min; aggregate.min; end
  def sum; aggregate.sum; end
  def mean; aggregate.mean; end
  def std_dev; aggregate.std_dev; end
  def outliers_low; aggregate.outliers_low; end
  def outliers_high; aggregate.outliers_high; end
  def to_s(*args); aggregate.to_s *args; end
  def each; aggregate.each { |*args| yield *args }; end
  def each_nonzero; aggregate.each_nonzero { |*args| yield *args }; end
end
