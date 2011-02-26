# -*- encoding: binary -*-
class Raindrops::Middleware::Proxy
  def initialize(body, stats)
    @body, @stats = body, stats
  end

  # yield to the Rack server here for writing
  def each
    @body.each { |x| yield x }
  end

  # the Rack server should call this after #each (usually ensure-d)
  def close
    @stats.decr_writing
    @body.close if @body.respond_to?(:close)
  end

  def to_path
    @body.to_path
  end

  def respond_to?(m)
    m = m.to_sym
    :close == m || @body.respond_to?(m)
  end
end
