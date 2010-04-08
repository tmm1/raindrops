# -*- encoding: binary -*-
require 'test/unit'
require 'raindrops'

class TestRaindropsGc < Test::Unit::TestCase

  def test_gc
    assert_nothing_raised do
      1000000.times { Raindrops.new(24) }
    end
  end

end
