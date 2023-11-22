#ifndef UTILS_SCOPED_FINALLY_H_
#define UTILS_SCOPED_FINALLY_H_

#include <utility>

template <typename F>
class ScopedFinally {
 public:
    explicit ScopedFinally(F f)
        : destruct_{std::move(f)} {}
    ~ScopedFinally() {
        destruct_();
    }

    ScopedFinally(const ScopedFinally &) = delete;
    ScopedFinally(ScopedFinally &&) = delete;
    void operator=(const ScopedFinally &) = delete;
    void operator=(ScopedFinally &&) = delete;

 private:
    F destruct_;
};

#endif  // UTILS_SCOPED_FINALLY_H_
