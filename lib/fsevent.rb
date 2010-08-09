require File.expand_path('../ext/fsevent', File.dirname(__FILE__))

class FSEvent
  attr_accessor :latency
  attr_reader   :directories

  def initialize(*args)
    @run_loop = CFRunLoop.instance or raise "could not obtain a run loop"
    @latency  = args.last.is_a?(Numeric) ? args.pop : 0.5
    watch args
  end

  def watch(*paths)
    @directories = paths.flatten.compact.map(&:to_str)
  rescue NoMethodError => e
    raise unless e.name == :to_str
    raise TypeError, "directories must be given as a String or an Array of strings"
  end
  alias_method :watch_directories, :watch
  alias_method :directories=,      :watch

  def restart
    stop
    start
  end

end
