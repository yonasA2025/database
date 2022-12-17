# Database
Overall structure of your code: The overall structure of my code is very similar to the process that was outlined in the handout. 
In db.c I implemented the thread safety and also changed a few function signatures in order to help this process. 
In server.c I filled out the code corresponding to the to-dos given to us in the strencil. 
I also in the main function decided not to use any helper methods and instead just call the functions that 
I created above the main method. I stored a few global variables but besides that nothing. 

Helper functions and what they do: I changed the db_search function signature to include and int 
syumbolizing whether the code was looking for a read lock or a write lock. 

Changes to any function signatures: There have been no changes to function signatures.
The only part of the stencil that I changed was the structs defintion to accomidate mutexes. 

Unresolved bugs: Currently there is still a deadlock when I run the program on a script over multiple threads, 
