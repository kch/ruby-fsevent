require File.expand_path('../ext/fsevent', File.dirname(__FILE__))

class FSEvent
  attr_accessor :latency
  attr_reader   :directories

  # call-seq:
  #   FSEvent.new(directory, …] [, latency])
  #
  # Create a new FSEvent notifier instance configured to receive notifications
  # for the given _directories_ with a specific _latency_. After the notifier
  # is created, you must call the +start+ method to begin receiving
  # notifications.
  #
  # It takes zero or more _directory_ arguments, which can be either a single
  # path or an array of paths. Each directory and all its sub-directories will
  # be monitored for file system modifications. The exact directory where the
  # event occurred will be passed to the user when the +changes+ method is
  # called.
  #
  # Clients can supply a Numeric _latency_ parameter as last argument, which
  # tells how long to wait after an event occurs before notifying; this
  # reduces the volume of events and reduces the chance that the client will
  # see an "intermediate" state.
  def initialize(*args)
    @run_loop = CFRunLoop.instance or raise "could not obtain a run loop"
    @latency  = args.last.is_a?(Numeric) ? args.pop : 0.5
    watch args
  end

  # Set a single directory or an array of directories to be monitored by this
  # fsevent notifier.
  def watch(*paths)
    @directories = paths.flatten.compact.map(&:to_str)
  rescue NoMethodError => e
    raise unless e.name == :to_str
    raise TypeError, "directories must be given as a String or an Array of strings"
  end
  alias_method :watch_directories, :watch
  alias_method :directories=,      :watch

  # Stop the event notification thread if running and then start it again. Any
  # changes to the directories to watch or the latency will be picked up when
  # the FSEvent notifier is restarted. Returns the FSEvent notifier instance.
  def restart
    stop
    start
  end

end
