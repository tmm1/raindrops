# -*- encoding: binary -*-
require 'raindrops'

# Raindrops middleware should be loaded at the top of Rack
# middleware stack before other middlewares for maximum accuracy.
class Raindrops
class Middleware < ::Struct.new(:app, :stats, :path, :tcp, :unix)

  # :stopdoc:
  Stats = Raindrops::Struct.new(:calling, :writing)
  PATH_INFO = "PATH_INFO"
  # :startdoc:

  def initialize(app, opts = {})
    super(app, opts[:stats] || Stats.new, opts[:path] || "/_raindrops")
    tmp = opts[:listeners]
    if tmp.nil? && defined?(Unicorn) && Unicorn.respond_to?(:listener_names)
      tmp = Unicorn.listener_names
    end

    if tmp
      self.tcp = tmp.grep(/\A[^:]+:\d+\z/)
      self.unix = tmp.grep(%r{\A/})
      self.tcp = nil if tcp.empty?
      self.unix = nil if unix.empty?
    end
  end

  # standard Rack endpoing
  def call(env)
    env[PATH_INFO] == path ? stats_response : dup._call(env)
  end

  def _call(env)
    stats.incr_calling
    status, headers, self.app = app.call(env)

    # the Rack server will start writing headers soon after this method
    stats.incr_writing
    [ status, headers, self ]
    ensure
      stats.decr_calling
  end

  # yield to the Rack server here for writing
  def each(&block)
    app.each(&block)
  end

  # the Rack server should call this after #each (usually ensure-d)
  def close
    stats.decr_writing
    ensure
      app.close if app.respond_to?(:close)
  end

  def stats_response
    body = "calling: #{stats.calling}\n" \
           "writing: #{stats.writing}\n"

    if defined?(Linux)
      Linux.tcp_listener_stats(tcp).each do |addr,stats|
        body << "#{addr} active: #{stats.active}\n" \
                "#{addr} queued: #{stats.queued}\n"
      end if tcp
      Linux.unix_listener_stats(unix).each do |addr,stats|
        body << "#{addr} active: #{stats.active}\n" \
                "#{addr} queued: #{stats.queued}\n"
      end if unix
    end

    headers = {
      "Content-Type" => "text/plain",
      "Content-Length" => body.size.to_s,
    }
    [ 200, headers, [ body ] ]
  end

end
end
