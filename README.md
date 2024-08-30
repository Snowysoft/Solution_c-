# Solution
## Task 1
### Design Decisions:

1. TCP Socket Communication: The solution utilizes TCP sockets for communication with a server running on localhost. Three separate socket connections are established to ports 4001, 4002, and 4003.

2. The program observes first the data generated at the three TCP ports in the 100ms window. Each value is followed by a newline character. The data is tokenized, and the value just before the last newline character (most recent value) is extracted.

3. JSON Output: The program prints received data in JSON format. Each JSON object contains a timestamp and three output strings (out1, out2, out3), which represent the latest data received from the server on each port.

4. I/O Handling with select: The program uses the select system call to monitor multiple sockets and a timer simultaneously. This allows the program to efficiently handle input from multiple sources, only processing sockets that have data available, while waiting on others without busy-waiting.

5. Timeout and Timestamps: The program uses a timeout of 100 milliseconds between iterations to control the frequency of data retrieval. Additionally, it retrieves the current timestamp in milliseconds since the Unix epoch to include in each JSON object.

### Solution Overview:

The solution consists of a C++ program (client1.cpp) responsible for establishing TCP socket connections to three separate ports (4001, 4002, and 4003) on localhost. It continuously receives data from each socket and prints the most recent data in JSON format to the standard output.

The program follows these steps:

1. Socket Creation and Connection: Three TCP socket connections are established to ports 4001, 4002, and 4003 on localhost.

2. A timer is set up using `timerfd_create´, and it is configured to trigger every 100 milliseconds. This timer drives the data collection loop.

3. The core of the program is a loop that repeatedly checks for activity on the sockets and the timer using select.

4. Data Processing: The program parses the received data and extracts the last line from each received message. It then updates the out1, out2, and out3 variables with the latest data received from each socket.

5. JSON Output: After processing the received data, the program constructs a JSON object containing the current timestamp and the latest values of out1, out2, and out3. This JSON object is printed to the standard output.

6. Upon termination, the program will close all open sockets and the timer.

#### <br> Additional File waveform_analysis.ipynb : <br>
This Python script reads waveform data from a JSON file, calculates the dominant frequency for each waveform using Fourier analysis, and visualizes the waveforms along with their timestamps using Matplotlib.

### Conclusion
| Output | Frequency (Hz)   | Amplitude | Shape           |
|--------|------------------|-----------|-----------------|
| out1   | 0.50             | 5         | Sine wave       |
| out2   | 0.24             | 5         | Triangle wave   |
| out3   | 0.               | 5         | Rectangular wave|

The frequency of out3 is zero because there is no repeating pattern observed in the waveform.

## Task 2
### Solution Overview:
1. Object and Property Field Encoding

The solution began with decoding the Operation field, but lacked information about the encoding of Object and Property fields. To address this, packets were sent with all possible Object IDs ranging from 1 to 0xFFFF. By monitoring the server's stdout responses, the following conclusions were drawn:

    Object ID 170 corresponds to amplitude.
    Object ID 255 corresponds to frequency.

2. Setting Value to 1Hz and 2Hz

The task specified setting the value to 1Hz and 2Hz, but encountered a discrepancy between the expected and actual behavior. The stdout message indicated an error due to the value being out of range, with the correct range being [50, 2000]. As a result, the assumption was made that the values should be interpreted as 1kHz and 2kHz respectively.

3. Verification of Solution Correctness

To validate the correctness of the solution, observations were made by monitoring the variation in the out1 terminal for different values of out3. This approach provided insight into the behavior of the system and confirmed the expected results based on the implemented solution.

### Conclusion
The solution for Task 2 involved decoding the Operation field, determining the encoding of Object and Property fields, and addressing discrepancies in setting values. By monitoring the system's behavior and verifying the observed outputs, confidence was gained in the correctness of the implemented solution.

## Compiling and Running the Code

1. **Compiling the Code:**

   -Navigate to the project directory.
   -Create a build directory and generate the build files using CMake

 ```
mkdir build
cd build
cmake ..
make
```

2. **Running the Executable:**

   After compiling, run the compiled executable using the following command:

   ```
   ./executable_name
   ```


