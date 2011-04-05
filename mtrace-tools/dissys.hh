#include <map>
#include <set>
#include <string>

#include "addr2line.hh"

using namespace::std;

class DistinctSyscalls : public EntryHandler {
public:
	virtual void handle(const union mtrace_entry *entry) {
		int cpu;

		if (mtrace_enable.access.value == 0)
			return;

		cpu = entry->h.cpu;

		if (entry->h.type == mtrace_entry_access) {
			const struct mtrace_access_entry *a = &entry->access;
			if (a->traffic)
				tag_to_distinct_set_[current_[cpu]].insert(a->guest_addr & ~63UL);
		} else if (entry->h.type == mtrace_entry_fcall) {
			const struct mtrace_fcall_entry *f = &entry->fcall;
			
			switch (f->state) {
			case mtrace_resume:
				current_[cpu] = f->tag;
				break;
			case mtrace_start:
				current_[cpu] = f->tag;
				tag_to_pc_[current_[cpu]] = f->pc;
				break;
			case mtrace_pause:
				current_[cpu] = 0;
				break;
			case mtrace_done:
				count_tag(current_[cpu]);
				current_[cpu] = 0;
				break;
			default:
				die("DistinctSyscalls::handle: default error");
			}
		}
		return;
	}

	virtual void exit(void) {
		while (tag_to_distinct_set_.size())
			count_tag(tag_to_distinct_set_.begin()->first);

		printf("%-32s %10s %10s %10s\n",
		       "function", "calls", "distinct", "ave");
		
		auto pit = pc_to_stats_.begin();
		for (; pit != pc_to_stats_.end(); ++pit) {
			uint64_t pc;
			char *func;
			char *file;
			int line;
			float n;

			pc = pit->first;
			n = (float)pit->second.distinct / 
				(float)pit->second.calls;
			
			if (pc == 0)
				printf("%-32s", "(unknown)");
			else if (addr2line->lookup(pc, &func, &file, &line) == 0) {
				printf("%-32s", func);
				free(func);
				free(file);
			} else
				printf("%-32lx", pc);

			printf("%10lu %10lu %10.2f\n", 
			       pit->second.calls, 
			       pit->second.distinct, n);
		}
	}

	int64_t distinct(const char *syscall) {
		auto pit = pc_to_stats_.begin();
		for (; pit != pc_to_stats_.end(); ++pit) {
			uint64_t pc;
			char *func;
			char *file;
			int line;

			pc = pit->first;

			if (addr2line->lookup(pc, &func, &file, &line) == 0) {
				if (strcmp(syscall, func) == 0)
					return pit->second.distinct;
			}
		}
		return -1;
	}

private:
	void count_tag(uint64_t tag) {
		uint64_t pc;
		uint64_t n;

		n = tag_to_distinct_set_[tag].size();
		tag_to_distinct_set_.erase(tag);
		pc = tag_to_pc_[tag];
		tag_to_pc_.erase(tag);

		if (pc_to_stats_.find(pc) == pc_to_stats_.end()) {
			pc_to_stats_[pc].distinct = 0;
			pc_to_stats_[pc].calls = 0;
		}
		pc_to_stats_[pc].distinct += n;
		pc_to_stats_[pc].calls++;
	}

	struct SysStats {
		uint64_t distinct;
		uint64_t calls;
	};

	map<uint64_t, uint64_t> tag_to_pc_;
	map<uint64_t, SysStats> pc_to_stats_;
	map<uint64_t, set<uint64_t> > tag_to_distinct_set_;

	// The current tag
	uint64_t current_[MAX_CPUS];
};

class DistinctOps : public EntryHandler {
public:
	DistinctOps(DistinctSyscalls *ds) : ds_(ds) {
		appname_to_syscalls_["procy"] = { "stub_clone", "sys_exit_group", "sys_wait4" };
	}

	virtual void exit(void) {
		map<string, set<const char *> >::iterator it = 
			appname_to_syscalls_.find(mtrace_summary.app_name);
		if (it == appname_to_syscalls_.end()) {
			fprintf(stderr, "DistinctOps::exit unable to find '%s'\n", 
				mtrace_summary.app_name);
			return;
		}

		set<const char *> *syscall = &it->second;		
		set<const char *>::iterator sysit = syscall->begin();
		uint64_t n = 0;
		for (; sysit != syscall->end(); ++sysit) {
			int64_t r = ds_->distinct(*sysit);
			if (r < 0)
				die("DistinctOps::exit: unable to find %s", *sysit);
			n += r;
		}

		float ave = (float)n / (float)mtrace_summary.app_ops;

		printf("%s ops: %lu distincts: %lu ave: %.2f\n",
		       mtrace_summary.app_name, mtrace_summary.app_ops, n, ave);
	}

private:
	DistinctSyscalls *ds_;
	map<string, set<const char *> > appname_to_syscalls_;

};
