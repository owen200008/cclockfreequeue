# CCLockfreeQueue FIFO linearizable
use microqueue like tbb & use circle array and circle index to make the performance better

# different from https://github.com/cameron314/concurrentqueue.git
concurrentqueue is fast and good enough, fast then 10M QPS, but it limit some use case. see the 
https://github.com/cameron314/concurrentqueue#reasons-to-use
https://github.com/cameron314/concurrentqueue#reasons-not-to-use

it is better enough，every thread have own producter，the less data race they have。it can make sure every thread push is FIFO linearizable，sometimes it is enough。

#benchmarks
in my computer v1231 4 core 8thread

4 threads
concurrentqueue push  35000/ms
concurrentqueue pop   7500/ms
CCLockfreeQueue push  15000/ms
CCLockfreeQueue pop   8000/ms

8 threads
concurrentqueue push  55000/ms
concurrentqueue pop   9000/ms
CCLockfreeQueue push  18000/ms
CCLockfreeQueue pop   8000/ms





