# Tic-Tac-Toe-online-game
The project involves creating an application that enables gameplay between two clients, where all data is transmitted through a server. The service operates using the TCP protocol and unicast addressing.
__________________________________
How does it works?
__________________________________
Upon connection, the client's data (entered nickname, IP address, number of wins/losses/draws) is saved to a file as a database. Additionally, the Player structure stores the nickname and IP address to send appropriate messages during the game (e.g., "Player's turn: nick_1"). When the server receives a connection from a client, it creates a new thread to handle that client.
The client and server operate in a loop, allowing for subsequent matches without the need to reconnect. Messages between the client and server are sent in TLV (Type-Length-Value) format. Upon connecting to the server, the client also creates a new thread to avoid blocking the reception of messages from the server while entering moves.
__________________________________
Game Flow
__________________________________
The client connects to the server and displays the following menu:
a) Play
b) View ranking
c) Exit
Option b) displays all players from the database, sorted by the number of wins (highest first).
Option c) ends the connection.
Option a) displays a prompt to enter a nickname. Each nickname is unique and linked in the database to the client’s IP address. It cannot be changed or impersonated.

After that, the server waits for a second player to join. When the player2_ip field in the Player structure is filled, the game begins. Both players receive an empty game board and a message indicating whose turn it is.
After a win, both clients are returned to the menu, and the server saves the result to the database.
__________________________________
Security
__________________________________
Protection against impersonation of other players (verification of nickname and player IP)

Handling of the SIGPIPE signal in case a player disconnects

Notifying players when the server closes the connection

Verification of the validity of moves
