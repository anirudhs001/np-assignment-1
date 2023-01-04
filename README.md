# IS F462: Network Programming Assignment 1

### Team Members: 
- [Anirudh Singh](https://github.com/anirudhs001) (2019A7PS0107P) 
- [Dhruv Rawat](https://github.com/thedhruvrawat) (2019B3A70537P)

## P1
Implement a command-line interpreter or shell. The shell takes in a command from user at its prompt and executes it and then prompts for more input from user. 

### Features
1. Shell should execute a command using `fork()` and `execve()` calls. It should not use temporary files, `popen()`,  or  `system()` calls.  It  should  not  use  `sh`  or  `bash`  shells  to  execute  a  command.  Once  the command is completed it should print its `pid` and `status`. 
2. Shell should support <, >, and >> redirection operators. 
3. Shell should support pipelining any number of commands. E.g.: `ls|wc|wc` or `cat x|grep pat|uniq|sort` 
4. Shell should support two new pipeline operators "||" and "|||". E.g.:  `ls -l || grep ^-, grep ^d`. It means  that output of `ls  -l`  command  is  passed  as  input  to  two  other  commands.  Similarly  "|||"  means, output of one command is passed as input to three other commands separated by ",".  
5. Implement  any  other  important  feature  not  covered  in  1  to  4 and  available  in  contemporary shells. 

Write a program called `shell.c` that implements the above requirements. 

### Deliverables: 
- shell.c 
- `pdf` explaining the design features for 1 to 4 and features selected for 5

## P2
Consider the requirement for a chat server which enables multiple users to send messages to each  other.  The  communication  between  server  and  clients  will  be  through  TCP  sockets.  Server  should  use message queues for IPC. It should not use temporary files or database to store data. Server can be tested using `telnet` command. 

### Features
1. A user should be able to join or leave the chat system. 
2. A user should be able to get the list of users with the status (whether online or not). 
3. A user should be able to send messages to any or all either online or offline. 
4. Server  should  create  a  new  process  for  every  client  connection  and  use  message  queues  for communicating among the children processes. 
5. Implement any other important feature not covered in 1 to 4 but available in chat apps

Write a program called `chat.c` that implements the above requirements. 

### Deliverables: 
- chat.c 
- `pdf` explaining the design features for 1 to 4 and feature selected for 5 