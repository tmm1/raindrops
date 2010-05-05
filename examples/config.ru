# sample stand-alone rackup application
require 'rack/lobster'
require 'raindrops'
use Raindrops::Middleware
run Rack::Lobster.new
