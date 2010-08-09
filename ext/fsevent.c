
#include "fsevent.h"

/*
 * Thanks to fswatch.c for providing a starting point
 * http://github.com/alandipert/fswatch
*/

void Init_cfrunloop _((void));

VALUE fsevent_class;
ID id_run_loop;
ID id_on_change;

// =========================================================================
// This method cleans up the resources used by our fsevent notifier. The run
// loop is stopped and released, and the notification stream is stopped and
// released. All our internal references are set to null.
static void
fsevent_struct_release( FSEvent fsevent )
{
  if (NULL == fsevent) return;

  if (fsevent->stream) {
    FSEventStreamStop(fsevent->stream);
    FSEventStreamInvalidate(fsevent->stream);
    FSEventStreamRelease(fsevent->stream);
  }
  fsevent->stream = 0;
}

// This method is called by the ruby garbage collector during the sweep phase
// of the garbage collection cycle. Memory allocated from the heap is freed
// and resources are released back to the mach kernel.
static void
fsevent_struct_free( void* ptr ) {
  if (NULL == ptr) return;

  FSEvent fsevent = (FSEvent) ptr;
  fsevent_struct_release(fsevent);

  xfree(fsevent);
}

// This method is called by the ruby interpreter when a new fsevent instance
// needs to be created. Memory is allocated from the heap and initialized to
// null values. A pipe is created at allocation time and lives for the
// duration of the instance.
static VALUE
fsevent_struct_allocate( VALUE klass ) {
  FSEvent fsevent = ALLOC_N( struct FSEvent_Struct, 1 );
  fsevent->stream = 0;
  return Data_Wrap_Struct( klass, NULL, fsevent_struct_free, fsevent );
}

// A helper method that will return true if the given VALUE is a
// fsevent_struct. Returns false if this is not the case.
bool
is_fsevent_struct( VALUE self ) {
  return TYPE(self) == T_DATA && RDATA(self)->dfree == (RUBY_DATA_FUNC) fsevent_struct_free;
}

// A helper method used to extract the fsevent C struct from the ruby "self" VALUE.
static FSEvent
fsevent_struct( VALUE self ) {
  FSEvent fsevent;

  if (!is_fsevent_struct(self)) rb_raise(rb_eTypeError, "expecting an FSEvent object");
  Data_Get_Struct( self, struct FSEvent_Struct, fsevent );
  return fsevent;
}

// This method is called by the ruby thread running in the CFRunLoop instance.
// When the system level callback (see method below) is run, it writes
// directory data to a pipe maintained by the CFRunLoop instance. The
// CFRunLoop thread (a ruby thread) reads that data from the pipe and invokes
// this callback.
//
// All we are doing here is safely passing information from the system level
// pthread to an internal ruby thread.
//
// This method then invokes the "on_change" method implemented by the user.
//
VALUE
fsevent_rb_callback( VALUE self, VALUE string ) {
  if (!RTEST(string)) return self;

  VALUE ary = rb_str_split(string, "\n");
  rb_funcall2(self, id_on_change, 1, &ary);
  return self;
}

// This method is called by the mach kernel to notify our instance that a
// filesystem change has occurred in one of the directories we are watching.
//
// The reference to our FSEvent ruby object is passed in as the second
// parameter. We extract the underlying fsevent struct from the ruby object
// and grab the write end of the pipe. The "eventPaths" we received from the
// kernel are written to this pipe as a single string with a newline "\n"
// character between each path.
//
// The user will call the "changes" method on the ruby object in order to read
// these paths out from the pipe when needed. The use of the pipe allows 1)
// the callback to return quickly to the kernel, and 2) for notifications to
// be queued in the pipe so they are not missed by the main ruby thread.
static void
fsevent_callback(
  ConstFSEventStreamRef streamRef,
  void* arg,
  size_t numEvents,
  void* eventPaths,
  const FSEventStreamEventFlags eventFlags[],
  const FSEventStreamEventId eventIds[]
) {
  VALUE self = (VALUE) arg;
  char** paths = eventPaths;
  int ii, io;
  long length = 0;

  io = cfrunloop_struct(rb_ivar_get(self, id_run_loop))->pipes[1];

  for (ii=0; ii<numEvents; ii++) { length += strlen(paths[ii]); }
  length += numEvents - 1;

  // send the object id and the length of the paths down the pipe
  write(io, (char*) &self, sizeof(VALUE));
  write(io, (char*) &length, sizeof(long));

  // send each path down the pipe separated by newlines
  for (ii=0; ii<numEvents; ii++) {
    if (ii > 0) { write(io, "\n", 1); }
    write(io, paths[ii], strlen(paths[ii]));
  }
}

// Start execution of the Mac CoreFramework run loop. This method is spawned
// inside a pthread so that the ruby interpreter is not blocked (the
// CFRunLoopRun method never returns).
static void
fsevent_start_stream( VALUE self ) {
  FSEvent fsevent = fsevent_struct(self);

  // convert our registered directory array into an OS X array of references
  VALUE ary = rb_iv_get(self, "@directories");
  int ii, length;
  length = RARRAY_LEN(ary);

  CFStringRef paths[length];
  for (ii=0; ii<length; ii++) {
    paths[ii] =
        CFStringCreateWithCString(
            NULL,
            (char*) RSTRING_PTR(RARRAY_PTR(ary)[ii]),
            kCFStringEncodingUTF8
        );
  }

  CFArrayRef pathsToWatch = CFArrayCreate(NULL, (const void **) &paths, length, NULL);
  CFAbsoluteTime latency = NUM2DBL(rb_iv_get(self, "@latency"));
  CFRunLoop cfrunloop = cfrunloop_struct(rb_ivar_get(self, id_run_loop));

  FSEventStreamContext context;
  context.version = 0;
  context.info = (void *) self;
  context.retain = NULL;
  context.release = NULL;
  context.copyDescription = NULL;

  fsevent->stream = FSEventStreamCreate(NULL,
    &fsevent_callback,
    &context,
    pathsToWatch,
    kFSEventStreamEventIdSinceNow,
    latency,
    kFSEventStreamCreateFlagNone
  );

  FSEventStreamScheduleWithRunLoop(fsevent->stream, cfrunloop->run_loop, kCFRunLoopDefaultMode);
  FSEventStreamStart(fsevent->stream);

  if (!cfrunloop->running) cfrunloop_signal(cfrunloop);
}

static VALUE fsevent_watch(VALUE, VALUE);


// FSEvent ruby methods start here

// =========================================================================

/* call-seq:
 *    start
 *
 * Start the event notification thread and begin receiving file system events
 * from the operating system. Calling this method multiple times will have no
 * ill affects. Returns the FSEvent notifier instance.
 */
static VALUE
fsevent_start( VALUE self ) {
  FSEvent fsevent = fsevent_struct(self);
  if (fsevent->stream) return self;

  VALUE ary = rb_iv_get(self, "@directories");
  Check_Type(ary, T_ARRAY);
  if (RARRAY_LEN(ary) <= 0) rb_raise(rb_eRuntimeError, "no directories to watch");

  fsevent_start_stream(self);
  return self;
}

/* call-seq:
 *    stop
 *
 * Stop the event notification thread. Calling this method multiple times will
 * have no ill affects. Returns the FSEvent notifier instance.
 */
static VALUE
fsevent_stop( VALUE self ) {
  FSEvent fsevent = fsevent_struct(self);
  fsevent_struct_release(fsevent);
  return self;
}

/* call-seq:
 *    running?
 *
 * Returns +true+ if the notification thread is running. Returns +false+ if
 * this is not the case.
 */
static VALUE
fsevent_is_running( VALUE self ) {
  FSEvent fsevent = fsevent_struct(self);
  if (fsevent->stream) return Qtrue;
  return Qfalse;
}

// =========================================================================
void Init_fsevent() {
  id_run_loop  = rb_intern("@run_loop");
  id_on_change = rb_intern("on_change");

  fsevent_class = rb_define_class( "FSEvent", rb_cObject );
  rb_define_alloc_func( fsevent_class, fsevent_struct_allocate );

  rb_define_method( fsevent_class, "stop",      fsevent_stop,        0 );
  rb_define_method( fsevent_class, "start",     fsevent_start,       0 );
  rb_define_method( fsevent_class, "running?",  fsevent_is_running,  0 );

  // initialize other classes
  Init_cfrunloop();
}
