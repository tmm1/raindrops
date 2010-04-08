# -*- encoding: binary -*-

class Raindrops::Struct

  def self.new(*members)
    members = members.map { |x| x.to_sym }.freeze
    str = <<EOS
def initialize(*values)
  (MEMBERS.size >= values.size) or raise ArgumentError, "too many arguments"
  @raindrops = Raindrops.new(MEMBERS.size)
  values.each_with_index { |val,i| @raindrops[i] = values[i] }
end

def initialize_copy(src)
  @raindrops = src.instance_variable_get(:@raindrops).dup
end

def []=(index, value)
  @raindrops[index] = value
end

def [](index)
  @raindrops[index]
end

def to_hash
  ary = @raindrops.to_ary
  rv = {}
  MEMBERS.each_with_index { |member, i| rv[member] = ary[i] }
  rv
end
EOS

    members.each_with_index do |member, i|
      str << "def incr_#{member}; @raindrops.incr(#{i}); end; " \
             "def decr_#{member}; @raindrops.decr(#{i}); end; " \
             "def #{member}; @raindrops[#{i}]; end; " \
             "def #{member}=(val); @raindrops[#{i}] = val; end; "
    end

    klass = Class.new
    klass.const_set(:MEMBERS, members)
    klass.class_eval(str)
    klass
  end

end
