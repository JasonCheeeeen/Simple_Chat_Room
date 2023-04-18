# Simple Chat Room

This project is a simple chat room.</br>
The chat room's server has two versions,</br>
</br>
Version-1 :</br> 
Single-Process Concurrent which use socket to transfer the message to users</br>
</br>
Version-2 :</br>
Concurrent Connection-Oriented, which use interprocess communication. </br>
In this version, it also use Shared Memory, FIFO and Signal to implement.</br>
</br>
In this chat room, the server support some basic shell functions which construct by myself and message transmission,</br>
so every user can request shell functions to get informations and transmit messages to other users.</br>
