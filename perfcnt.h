#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>

#include <linux/perf_event.h>

struct perf_counters {
	uint64_t n;          // number of fields below
	uint64_t nsec, cycles, insns;
};

static int perf_fd = -1;

static ssize_t sys_read(int fd, void *buf, size_t size) {
	ssize_t r;
#ifdef __x86_64__
	__asm__ __volatile__("syscall" : "=a"(r)
				: "a"(SYS_read), "D"(fd), "S"(buf), "d"(size)
				: "%rcx", "%r11", "memory");
#else
	r = syscall(SYS_read, fd, buf, size);
	// r = read(fd, buf, size);
#endif
	return r;
}

static int perf_event_open(
		void *attr, pid_t pid, int cpu, int groupfd, unsigned long flags) {
	return syscall(SYS_perf_event_open, attr, pid, cpu, groupfd, flags);
}

static int perf_init(void) {
	struct perf_event_attr evt = {
		.size = sizeof(evt),
		.type = PERF_TYPE_SOFTWARE,
		.config = PERF_COUNT_SW_CPU_CLOCK,
		.read_format = PERF_FORMAT_GROUP,
		.pinned = 1,
		.exclude_kernel = 1,
		.exclude_hv     = 1,
	};
	int fd;

	do {
		if ((fd = perf_event_open(&evt, 0, -1, -1, 0)) < 0) break;
		evt.type = PERF_TYPE_HARDWARE;
		evt.config = PERF_COUNT_HW_CPU_CYCLES;
		evt.pinned = 0;
		if (perf_event_open(&evt, 0, -1, fd, 0) < 0) break;
		evt.config = PERF_COUNT_HW_INSTRUCTIONS;
		if (perf_event_open(&evt, 0, -1, fd, 0) < 0) break;
		if (ioctl(fd, PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP) < 0) break;
		perf_fd = fd;
		return fd;
	} while (0);
	{
		const char *path = "/proc/sys/kernel/perf_event_paranoid";
		char c;
		if ((fd = open(path, O_RDONLY)) >= 0
			  && read(fd, &c, 1) == 1 && c > '2')
			printf(
	"Some distributions (e.g. Debian and derivatives) disallow self-profiling\n\n"
	"Run 'sysctl kernel.perf_event_paranoid=2' as root or write to %s manually.\n",
				     path);
		close(fd);
	}
	return -1;
}

static int perf_read(struct perf_counters *cnt) {
	return sys_read(perf_fd, cnt, sizeof(*cnt));
}

#define TIMER_DEF \
	uint64_t time = 0, cycles = 0; \
	double min_cycles = 1e9, min_insns = 1e9; \
	struct perf_counters perf0, perf1;

#define TIMER_INIT \
	if (perf_init() < 0) return 2;

#define TIMER_START \
	perf_read(&perf0);

#define TIMER_STOP { \
	perf_read(&perf1); \
	cycles += perf1.cycles - perf0.cycles; \
	double cycles = (double)(perf1.cycles - perf0.cycles) / n; \
	if (min_cycles > cycles) { \
		min_cycles = cycles; \
		min_insns = (double)(perf1.insns - perf0.insns) / n; \
	} \
	time += perf1.nsec - perf0.nsec; \
}

#define TIMER_PRINT \
	printf(" %s: %.3fms", type, time * 1e-6); \
	printf(", %.3f cycles/byte, %.3f insn/byte (%.3f GHz)", \
			min_cycles, min_insns, 1.0 * (int64_t)cycles / (int64_t)time);

