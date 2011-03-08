# -*- encoding: binary -*-
require "socket"
#
# Used to aggregate last_data_recv times
module Raindrops::Aggregate::LastDataRecv
  TCP_Info = Raindrops::TCP_Info
  attr_accessor :raindrops_aggregate
  @@default_aggregate = nil

  def self.default_aggregate
    @@default_aggregate ||= Raindrops::Aggregate::PMQ.new
  end

  def self.default_aggregate=(agg)
    @@default_aggregate = agg
  end

  def self.cornify!
    Unicorn::HttpServer::LISTENERS.each do |sock|
      sock.extend(self) if TCPServer === sock
    end
  end

  def self.extended(obj)
    obj.raindrops_aggregate = default_aggregate
    obj.setsockopt Socket::SOL_TCP, tcp_defer_accept = 9, seconds = 60
  end

  def kgio_tryaccept(*args)
    count! super
  end

  def kgio_accept(*args)
    count! super
  end

  def accept
    count! super
  end

  def accept_nonblock
    count! super
  end

  def count!(io)
    if io
      x = TCP_Info.new(io)
      @raindrops_aggregate << x.last_data_recv
    end
    io
  end
end

