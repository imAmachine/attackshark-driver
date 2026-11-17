#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

static int fake_ioctl(int fd, unsigned long req, void *buf);
static int cleanup_timer(timer_t timer);
static int create_timer(clockid_t clock, struct sigevent *event, timer_t *timer);
static int arm_timer(timer_t timer, int flags, const struct itimerspec *value,
                     struct itimerspec *old);
#define ioctl fake_ioctl
#define timer_create create_timer
#define timer_settime arm_timer
#define timer_delete cleanup_timer
#include "../src/hidraw.c"
#undef timer_create
#undef timer_settime
#undef timer_delete
#undef ioctl

static int pipefd[2];
static volatile sig_atomic_t alarms;
static _Thread_local int mode;
static atomic_int finished;
static atomic_int blocked_entered;
static atomic_int blocked_finished;
static int failure;

static int create_timer(clockid_t clock, struct sigevent *event, timer_t *timer)
{
    if (failure == 1) {
        errno = EAGAIN;
        return -1;
    }
    return timer_create(clock, event, timer);
}

static int arm_timer(timer_t timer, int flags, const struct itimerspec *value,
                     struct itimerspec *old)
{
    if (failure == 2) {
        errno = EINVAL;
        return -1;
    }
    return timer_settime(timer, flags, value, old);
}

static int cleanup_timer(timer_t timer)
{
    int ret = timer_delete(timer);
    errno = EDOM;
    return ret;
}

static int fake_ioctl(int fd, unsigned long req, void *buf)
{
    (void)req;
    if (mode == 1) {
        errno = EIO;
        return -1;
    }
    if (mode == 2) {
        return 1;
    }
    if (mode == 3) {
        errno = EINTR;
        return -1;
    }
    if (mode == 5) {
        sigset_t blocked;
        sigemptyset(&blocked);
        sigaddset(&blocked, SIGRTMAX);
        assert(pthread_sigmask(SIG_BLOCK, &blocked, NULL) == 0);
        sleep(1);
        return 1;
    }
    if (mode == 4) {
        struct timespec delay = { .tv_nsec = 60000000 };
        nanosleep(&delay, NULL);
    }
    if (mode == 6) {
        atomic_store(&blocked_entered, 1);
    }
    return (int)read(fd, buf, 1);
}

static void on_signal(int sig)
{
    if (sig == SIGALRM) {
        alarms++;
    }
}

static void sleep_ms(int ms)
{
    struct timespec delay = { ms / 1000, (ms % 1000) * 1000000L };
    while (nanosleep(&delay, &delay) < 0 && errno == EINTR) {
    }
}

static void *worker(void *arg)
{
    struct as_hid dev = { .fd = pipefd[0] };
    unsigned char buf;
    int ret;
    sigset_t before, after;
    (void)arg;
    assert(pthread_sigmask(SIG_SETMASK, NULL, &before) == 0);
    ret = as_feature_get(&dev, &buf, 1, 35);
    assert(ret == -ETIMEDOUT);
    assert(pthread_sigmask(SIG_SETMASK, NULL, &after) == 0);
    for (int sig = 1; sig < NSIG; sig++) {
        assert(sigismember(&before, sig) == sigismember(&after, sig));
    }
    atomic_store(&finished, 1);
    return NULL;
}

static void *interrupted_worker(void *arg)
{
    struct as_hid dev = { .fd = pipefd[0] };
    unsigned char buf;
    (void)arg;
    assert(as_feature_get(&dev, &buf, 1, 1000) == -EINTR);
    return NULL;
}

static void *blocked_worker(void *arg)
{
    struct as_hid dev = { .fd = pipefd[0] };
    unsigned char buf;
    (void)arg;
    mode = 6;
    assert(as_feature_get(&dev, &buf, 1, 3000) == 0);
    atomic_store(&blocked_finished, 1);
    return NULL;
}

int main(void)
{
    struct sigaction sa = { .sa_handler = on_signal }, observed;
    struct itimerval alarm_timer = { .it_interval = { 0, 10000 },
                                    .it_value = { 0, 10000 } }, remaining;
    struct as_hid dev;
    sigset_t original, blocked, after;
    pthread_t thread, threads[4];
    unsigned char buf;
    int signo = SIGRTMAX;
    assert(pipe(pipefd) == 0);
    dev.fd = pipefd[0];
    sigemptyset(&sa.sa_mask);
    assert(sigaction(SIGALRM, &sa, NULL) == 0);
    assert(sigaction(SIGUSR1, &sa, NULL) == 0);
    assert(setitimer(ITIMER_REAL, &alarm_timer, NULL) == 0);
    assert(pthread_create(&thread, NULL, worker, NULL) == 0);
    sleep_ms(200);
    int completed_on_time = atomic_load(&finished);
    if (!completed_on_time) {
        pthread_kill(thread, SIGUSR1);
    }
    assert(pthread_join(thread, NULL) == 0);
    assert(completed_on_time);
    assert(alarms >= 5);
    assert(sigaction(SIGALRM, NULL, &observed) == 0);
    assert(observed.sa_handler == on_signal);
    assert(getitimer(ITIMER_REAL, &remaining) == 0);
    assert(remaining.it_interval.tv_usec == 10000);
    assert(remaining.it_value.tv_sec || remaining.it_value.tv_usec);
    alarm_timer = (struct itimerval){ 0 };
    assert(setitimer(ITIMER_REAL, &alarm_timer, NULL) == 0);
    sigemptyset(&blocked);
    sigaddset(&blocked, signo);
    assert(pthread_sigmask(SIG_BLOCK, &blocked, &original) == 0);
    for (mode = 1; mode <= 3; mode++) {
        int expected = mode == 1 ? -EIO : mode == 2 ? 0 : -EINTR;
        assert(as_feature_get(&dev, &buf, 1, 20) == expected);
        assert(pthread_sigmask(SIG_SETMASK, NULL, &after) == 0);
        assert(sigismember(&after, signo) == 1);
        assert(sigaction(signo, NULL, &observed) == 0);
        assert(observed.sa_handler == SIG_DFL);
    }
    for (failure = 1; failure <= 2; failure++) {
        assert(as_feature_get(&dev, &buf, 1, 20) ==
               (failure == 1 ? -EAGAIN : -EINVAL));
        assert(pthread_sigmask(SIG_SETMASK, NULL, &after) == 0);
        assert(sigismember(&after, signo) == 1);
        assert(sigaction(signo, NULL, &observed) == 0);
        assert(observed.sa_handler == SIG_DFL);
    }
    failure = 0;
    mode = 5;
    assert(as_feature_get(&dev, &buf, 1, 20) == 0);
    assert(sigpending(&after) == 0);
    assert(!sigismember(&after, signo));
    mode = 2;
    assert(raise(signo) == 0);
    assert(as_feature_get(&dev, &buf, 1, 20) == 0);
    assert(sigpending(&after) == 0);
    assert(sigismember(&after, signo));
    assert(sigwaitinfo(&blocked, NULL) == signo);
    mode = 4;
    assert(as_feature_get(&dev, &buf, 1, 20) == -ETIMEDOUT);
    mode = 2;
    for (int i = 0; i < 1000; i++) {
        assert(as_feature_get(&dev, &buf, 1, 1) == 0);
    }
    mode = 3;
    assert(as_feature_get(&dev, &buf, 1, 0) == -EINTR);
    assert(pthread_sigmask(SIG_SETMASK, &original, NULL) == 0);
    assert(pthread_create(&thread, NULL, interrupted_worker, NULL) == 0);
    sleep_ms(50);
    assert(pthread_kill(thread, SIGUSR1) == 0);
    assert(pthread_join(thread, NULL) == 0);
    assert(pthread_create(&thread, NULL, blocked_worker, NULL) == 0);
    for (int i = 0; i < 1000 && !atomic_load(&blocked_entered); i++) {
        sleep_ms(1);
    }
    assert(atomic_load(&blocked_entered));
    mode = 2;
    struct as_hid quick_dev = { .fd = pipefd[1] };
    assert(as_feature_get(&quick_dev, &buf, 1, 100) == 0);
    assert(!atomic_load(&blocked_finished));
    buf = 1;
    assert(write(pipefd[1], &buf, 1) == 1);
    assert(pthread_join(thread, NULL) == 0);
    assert(atomic_load(&blocked_finished));
    assert(sigaction(signo, NULL, &observed) == 0);
    assert(observed.sa_handler == SIG_DFL);
    assert(sigaction(signo - 1, NULL, &observed) == 0);
    assert(observed.sa_handler == SIG_DFL);
    assert(sigaction(signo, &sa, NULL) == 0);
    for (int i = 0; i < 4; i++) {
        assert(pthread_create(&threads[i], NULL, worker, NULL) == 0);
    }
    for (int i = 0; i < 4; i++) {
        assert(pthread_join(threads[i], NULL) == 0);
    }
    assert(sigaction(signo, NULL, &observed) == 0);
    assert(observed.sa_handler == on_signal);
    mode = 2;
    for (int sig = SIGRTMIN; sig <= SIGRTMAX; sig++) {
        assert(sigaction(sig, &sa, NULL) == 0);
    }
    assert(as_feature_get(&dev, &buf, 1, 20) == -EBUSY);
    sa.sa_handler = SIG_DFL;
    for (int sig = SIGRTMIN; sig <= SIGRTMAX; sig++) {
        assert(sigaction(sig, &sa, NULL) == 0);
    }
    close(pipefd[0]);
    close(pipefd[1]);
    puts("transport tests passed");
    return 0;
}
