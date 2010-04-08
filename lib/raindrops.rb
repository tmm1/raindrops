# -*- encoding: binary -*-
class Raindrops

  # Used to represent the number of +active+ and +queued+ sockets for
  # a single listen socket across all threads and processes on a
  # machine.
  #
  # For TCP listeners, only sockets in the TCP_ESTABLISHED state are
  # accounted for.  For Unix domain listeners, only CONNECTING and
  # CONNECTED Unix domain sockets are accounted for.
  #
  # +active+ connections is the number of accept()-ed but not-yet-closed
  # sockets in all threads/processes sharing the given listener.
  #
  # +queued+ connections is the number of un-accept()-ed sockets in the
  # queue of a given listen socket.
  #
  # These stats are currently only available under Linux
  class ListenStats < Struct.new(:active, :queued)

    # the sum of +active+ and +queued+ sockets
    def total
      active + queued
    end
  end

  # TODO: pure Ruby version for single processes
  require 'raindrops_ext'

  autoload :Struct, 'raindrops/struct'
  autoload :Middleware, 'raindrops/middleware'
end
