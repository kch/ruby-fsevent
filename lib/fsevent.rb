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
    @directories = paths.flatten.map(&:to_str)
  end
  alias_method :watch_directories, :watch
  alias_method :directories=,      :watch

  def restart
    stop
    start
  end

end
