# -*- encoding: binary -*-
require 'test/unit'
require 'raindrops'

class TestRaindropsGc < Test::Unit::TestCase

  # we may need to create more garbage as GC may be less aggressive
  # about expiring things.  This is completely unrealistic code,
  # though...
  def test_gc
    assert_nothing_raised do
      1000000.times { |i| Raindrops.new(24); [] }
    end
  end

end
