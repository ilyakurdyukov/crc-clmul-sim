#include <stdint.h>

struct perf_counters {
	uint64_t n;          // number of fields below
	uint64_t nsec;
	uint64_t cycles;
	uint64_t instructions;
};

#ifndef __linux__
static int perf_setup(void)
{
	return -1;
}
static int perf_measure(int fd, struct perf_counters *cnt)
{
	return -1;
}
static void perf_hint(void)
{
}
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>

#include <linux/perf_event.h>

static ssize_t
sys_read(int fd, void *buf, size_t size)
{
	ssize_t r;
#ifdef __x86_64__
	asm volatile ("syscall"
		      : "=a"(r)
		      : "0"(SYS_read), "D"(fd), "S"(buf), "d"(size)
		      : "%rcx", "%r11", "memory");
#else
#error unhandled architecture
	r = read(fd, buf, size);
#endif
	return r;
}

static int
perf_event_open(void *attr, pid_t pid, int cpu, int groupfd, unsigned long flags)
{
	return syscall(SYS_perf_event_open, attr, pid, cpu, groupfd, flags);
}

static int perf_setup(void)
{
	struct perf_event_attr evt = {
		.size = sizeof evt,
		.type = PERF_TYPE_SOFTWARE,

		.config = PERF_COUNT_SW_CPU_CLOCK,

		.read_format = PERF_FORMAT_GROUP,

		.pinned = 1,
		.exclude_kernel = 1,
		.exclude_hv     = 1,
	};
	int fd;

	if ((fd = perf_event_open(&evt, 0, -1, -1, 0)) < 0)
		return -1;
	evt.type = PERF_TYPE_HARDWARE;
	evt.config = PERF_COUNT_HW_CPU_CYCLES;
	evt.pinned = 0;
	if (perf_event_open(&evt, 0, -1, fd, 0) < 0)
		return -1;
	evt.config = PERF_COUNT_HW_INSTRUCTIONS;
	if (perf_event_open(&evt, 0, -1, fd, 0) < 0)
		return -1;
	if (ioctl(fd, PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP) < 0)
		return -1;
	return fd;
}

static int
perf_measure(int fd, struct perf_counters *cnt)
{
	return sys_read(fd, cnt, sizeof *cnt);
}

static void perf_hint(void)
{
	const char *path = "/proc/sys/kernel/perf_event_paranoid";
	int fd;
	char c;
	if ((fd = open(path, O_RDONLY)) >= 0
	    && read(fd, &c, 1) == 1
	    && c > '2')
		printf(
"Some distributions (e.g. Debian and derivatives) disallow self-profiling\n\n"
"Run 'sysctl kernel.perf_event_paranoid=2' as root or write to %s manually.\n",
		       path);
	close(fd);
}
#endif
