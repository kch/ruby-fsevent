$LOAD_PATH.unshift File.expand_path('../lib', File.dirname(__FILE__))
require 'fsevent'

directories = %W[ #{Dir.pwd} /tmp ]

puts "watching #{directories.join(", ")} for changes"

FSEvent.watch(directories, 0.2) { |dirs| puts "Detected change in: #{dirs.inspect}" }

# You have to get the main ruby thread to sleep or wait on an IO stream
# otherwise the ruby interpreter will exit. Here we'll just sleep.
# You can stop the program by sending ^C to it.
sleep
