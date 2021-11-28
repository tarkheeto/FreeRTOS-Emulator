# FreeRTOS Emulator Exercise Submission
The skeleton that I had built the solutions to the exercises on was the Demo Clean 

# Exercise 2: 

  In exercise two there are mainly two tasks, the first task does the drawing and the second increments a variable (equivalent to an angle used by sin/cos operators for the rotational motion). Pressing on A,B,C or D will increment an on screen counter and pressing on the left mouse button will reset it. In this exercise and in all the others pressing E will change the state of an FSM Machine ( the code of which was taken from the Master Demo ( along with alot of code parts tbh such as the framerate)).

For the moving text; the text takes in the horizontal part of the rotation of the circle. 

# Exercise 3: 

Exercise three was designed exactly as the tasks asked. The only extra thing I had was that (because of my lack of attention) I had also implemented a triggerable timer (without the help of any extra tasks) that increments a variable every second. After that I added the task that does the same (the one that is mentioned in 3.2.4) The program prints on screen instructions so it shouldnt be vague.

What is a Kernal tick ? -> "The kernel "tick" frequency is all about how often the kernel should check what process is running"- raspberrypi.org
What is a tickless kernel ? -> "A tickless kernel is an operating system kernel in which timer interrupts do not occur at regular intervals, but are only delivered as required."- Wikipedia

I did not know if I should straight up use my own words or not, but the definitions were precise and straight to the point :) .

# Exercise 4:
What I observed from reversing the priorities was that sometimes the Ex4_3 task would get delayed for more than 1 tick after the execution of Ex4_2 (the one that gives the semaphore that Ex4_3 takes )
