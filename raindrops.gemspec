# -*- encoding: binary -*-

ENV["VERSION"] or abort "VERSION= must be specified"
manifest = File.readlines('.manifest').map! { |x| x.chomp! }
test_files = manifest.grep(%r{\Atest/test_.*\.rb\z})

Gem::Specification.new do |s|
  s.name = %q{raindrops}
  s.version = ENV["VERSION"]

  s.authors = ["raindrops hackers"]
  s.date = Time.now.utc.strftime('%Y-%m-%d')
  s.description = File.read("README").split(/\n\n/)[1]
  s.email = %q{raindrops@librelist.com}
  s.extensions = %w(ext/raindrops/extconf.rb)

  s.extra_rdoc_files = File.readlines('.document').map! do |x|
    x.chomp!
    if File.directory?(x)
      manifest.grep(%r{\A#{x}/})
    elsif File.file?(x)
      x
    else
      nil
    end
  end.flatten.compact

  s.files = manifest
  s.homepage = %q{http://raindrops.bogomips.org/}
  s.summary = %q{real-time stats for preforking Rack servers}
  s.rdoc_options = [ "-Na", "-t", "raindrops - #{s.summary}" ]
  s.require_paths = %w(lib)
  s.rubyforge_project = %q{raindrops}

  s.test_files = test_files

  # s.licenses = %w(LGPLv3) # accessor not compatible with older RubyGems
end
