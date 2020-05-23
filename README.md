# Producer-Consumer Problem Using Inter-Process Communication (IPC)

This repository simulates synchronized producer and consumer processes. The producer produces items and places them into a shared bounded buffer as long as it is not full, and the consumer consumes the produced items in the shared bounded buffer as long as it is not empty. The producer and the consumer use inter-process communication.

Both the size of the bounded buffer as well as the rates at which the producer/consumer are to produce/consume items are specified by the user. 

Please refer to the document "Task Description.pdf" for a detailed description of the task.

# Implementation Assumptions
- The item is the number of the occupied slots in the buffer so far (before adding the one currently produced).
- The first of the two processes to exit, detaches from the memory.
- The last process to exit, destroys the shared memory and the three semaphores.
- In case the producer exited and the consumer is still running, the consumer exits when it consumes all the items in the buffer.
- In case the consumer exited and the producer is still running, the producer exits when it fills all the empty slots in the buffer.
