class FSEvent
  def singleton_class #:nodoc:
    class << self; self end
  end unless method_defined? :singleton_class
end
