# -*- encoding: binary -*-
class Raindrops
module Linux

  # The standard proc path for active UNIX domain sockets, feel free to call
  # String#replace on this if your /proc is mounted in a non-standard location
  # for whatever reason
  PROC_NET_UNIX_ARGS = %w(/proc/net/unix)
  defined?(::Encoding) and PROC_NET_UNIX_ARGS.push({ :encoding => "binary" })

  # Get ListenStats from an array of +paths+
  #
  # Socket state mapping from integer => symbol, based on socket_state
  # enum from include/linux/net.h in the Linux kernel:
  #     typedef enum {
  #             SS_FREE = 0,              /* not allocated                */
  #             SS_UNCONNECTED,           /* unconnected to any socket    */
  #             SS_CONNECTING,            /* in process of connecting     */
  #             SS_CONNECTED,             /* connected to socket          */
  #             SS_DISCONNECTING          /* in process of disconnecting  */
  #     } socket_state;
  # * SS_CONNECTING maps to ListenStats#active
  # * SS_CONNECTED maps to ListenStats#queued
  #
  # This method may be significantly slower than its tcp_listener_stats
  # counterpart due to the latter being able to use inet_diag via netlink.
  # This parses /proc/net/unix as there is no other (known) way
  # to expose Unix domain socket statistics over netlink.
  def unix_listener_stats(paths)
    rv = Hash.new { |h,k| h[k.freeze] = ListenStats.new(0, 0) }
    paths = paths.map do |path|
      path = path.dup
      path.force_encoding(Encoding::BINARY) if defined?(Encoding)
      rv[path]
      Regexp.escape(path)
    end
    paths = / 00000000 \d+ (\d+)\s+\d+ (#{paths.join('|')})$/n

    # no point in pread since we can't stat for size on this file
    File.read(*PROC_NET_UNIX_ARGS).scan(paths) do |s|
      path = s.last
      case s.first.to_i
      when 2 then rv[path].queued += 1
      when 3 then rv[path].active += 1
      end
    end

    rv
  end

  module_function :unix_listener_stats

end # Linux
end # Raindrops
