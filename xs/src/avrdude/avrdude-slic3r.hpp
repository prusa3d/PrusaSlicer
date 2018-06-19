#ifndef slic3r_avrdude_slic3r_hpp_
#define slic3r_avrdude_slic3r_hpp_

#include <memory>
#include <vector>
#include <string>
#include <functional>

namespace Slic3r {

class AvrDude
{
public:
	typedef std::shared_ptr<AvrDude> Ptr;
	typedef std::function<void()> RunFn;
	typedef std::function<void(const char * /* msg */, unsigned /* size */)> MessageFn;
	typedef std::function<void(const char * /* task */, unsigned /* progress */)> ProgressFn;
	typedef std::function<void(int /* exit status */)> CompleteFn;

	AvrDude();
	AvrDude(AvrDude &&);
	AvrDude(const AvrDude &) = delete;
	AvrDude &operator=(AvrDude &&) = delete;
	AvrDude &operator=(const AvrDude &) = delete;
	~AvrDude();

	// Set location of avrdude's main configuration file
	AvrDude& sys_config(std::string sys_config);

	// Set avrdude cli arguments
	AvrDude& args(std::vector<std::string> args);

	// Set a callback to be called just after run() before avrdude is ran
	// This can be used to perform any needed setup tasks from the background thread.
	// This has no effect when using run_sync().
	AvrDude& on_run(RunFn fn);

	// Set message output callback
	AvrDude& on_message(MessageFn fn);

	// Set progress report callback
	// Progress is reported per each task (reading / writing) in percents.
	AvrDude& on_progress(ProgressFn fn);

	// Called when avrdude's main function finishes
	AvrDude& on_complete(CompleteFn fn);

	int run_sync();
	Ptr run();

	void cancel();
	void join();
private:
	struct priv;
	std::unique_ptr<priv> p;
};


}

#endif
