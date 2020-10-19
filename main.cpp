#include <iostream>
#include <thread>
#include <vector>
#include <mutex>
#include <chrono>
#include <array>
#include <random>
#include <map>
#include <termios.h>
#include <unistd.h>

#define CLEAR_SCREEN_CODE "\033[2J\033[1;1H"
#define KNRM  "\x1B[0m"
#define KRED  "\x1B[31m"
#define KGRN  "\x1B[32m"
#define KYEL  "\x1B[33m"
#define KBLU  "\x1B[34m"
#define KMAG  "\x1B[35m"
#define KCYN  "\x1B[36m"
#define KWHT  "\x1B[37m"

std::array<std::array<char, 24>, 10> board;
std::mutex board_mutex;
int piece_on_board_index = 0; //To P can be shown as its correct color
std::vector<int> piece_history; //To avoid RNG spamming the same block over and over
bool paused;
bool tetris_exit;
int difficulty;
int level;
int lines_cleared;
int score;
int rotation_count = 0; //Keeps a log of how many times a piece has been rotated

std::array<std::array<char, 24>, 10> board_last_state;
const std::map<int, int> level_to_drop_rate = {{0, 53}, {1,49}, {2,45}, {3,41}, {4,37}, {5,33}, {6,28}, {7,22}, {8,17}, {9,11}, {10,10}, {11,9}, {12,8}, {13,7}, {14,6}, {15,6}, {16,5}, {17,5}, {18,4}, {19,4}, {20,3}}; //https://harddrop.com/wiki/Tetris_(Game_Boy)
const std::map<int, int> cleared_lines_to_score = {{0,0}, {1, 40}, {2, 100}, {3, 300}, {4, 1200}};
const std::map<int, char> piece_index_to_char = {{0, 'I'}, {1, 'O'}, {2, 'L'}, {3, 'J'}, {4, 'T'}, {5, 'S'}, {6, 'Z'}};
const std::map<char, int> piece_char_to_index = {{'I', 0}, {'O', 1}, {'L', 2}, {'J', 3}, {'T', 4}, {'S', 5}, {'Z', 6}};
const std::map<int, std::string> piece_color = {{0, KRED}, {1, KYEL}, {2, KMAG}, {3, KWHT}, {4, KBLU}, {5, KCYN}, {6, KGRN}};
int lines_to_level_increase = 5;
bool stop_read_input;

bool spawn_piece(int override_rng=-1);
int make_piece_fall(char piece, bool lock_mutex=false);
void print_map(bool only_print_once=false, bool force_reprint=false);
void read_input(char override_input=' ');
bool is_space_free(std::vector<std::pair<int, int>> pieces);
std::vector<std::pair<int, int>> find_instance(char piece);
inline void ms_sleep(int ms);

//Line - I Bright Red/9
//Block - O Bright Yellow/11
//L piece - L Bright Purple/13
//Mirror L - J White/15
//Pirymid - T Bright Blue/12
//Up Stairs - S Bright Cyan/1
//Down stairs - Z Bright
//Down stairs - Z Bright Green/10
//Object all the way down representation - U
//Piece of intrest - P

int main() {
	for (int y = 0; y < 24; y++) {   
		for (int x = 0; x < 10; x++) {
			board[x][y]	= ' ';
		}
	} //Fill with ' '.
	
	//make input raw
	struct termios config;
	tcgetattr(STDIN_FILENO, &config);
	struct termios config_backup;
	config_backup = config;
	//config.c_iflag &= ~(IGNBRK | BRKINT | ICRNL | INLCR | PARMRK | INPCK | ISTRIP | IXON); //cfmakeraw()
	cfmakeraw(&config);
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &config);

	print_map(true, true);
	std::thread input_read_thread(read_input, ' '); //read input and print map threads
	std::thread map_print_thread(print_map, false, false);

	while (true) { 
		if (!spawn_piece()) {
			tetris_exit = true;
			input_read_thread.detach(); //Bad practice, they dont deconstuct properly... well we will crash them when they try to lock the mutex were are about to destroy
			map_print_thread.detach();
			board_mutex.lock();
			auto board_backup = board; //backup board incase it currupts during the next step
			board_mutex.unlock();
			pthread_mutex_destroy(board_mutex.native_handle()); //Bad practice, force destroy the mutex. Could cause curruption.
			tcsetattr(STDIN_FILENO, TCSAFLUSH, &config_backup);
			//restore backup
			bool currupted;
			if (board != board_backup) {
				currupted = true;
			}
			board = board_backup;
			print_map(true, true); //Reprint after threads are killed
			
			//Block animation
			for (int line=0; line < 20; line++) {
				for (int x=0; x<10; x++) {
					board[x][line] = 'J';
				}
				print_map(true, false);
				ms_sleep(100);
			}
			for (int line=0; line < 20; line++) {
				for (int x=0; x<10; x++) {
					board[x][line] = 'B';
				}
				print_map(true, false);
				ms_sleep(100);
			}

			std::cout << CLEAR_SCREEN_CODE << std::flush;
			system ("/bin/stty cooked");
			//TODO: Cleanup code below
			std::cout << "\033[7A" << "You Loose!\r\n";
			std::cout << "\bFinal Score: " << score << "\r\n";
			std::cout << "You reached Level: " << level << "\r\n";
			std::cout << KBLU << "I have no idea what made Mr. Moon regards this project as being so impressive to warrant a Facalty and Staff email but hey, i'll take it.\r\n" << KNRM;
			std::cout << KYEL << "Thanks for playing.\r\n" << KNRM;
			std::cout << KGRN << "Last Edited: 3/5/2020" << std::endl;
			std::cout << "Future Plans:\nMenu with animations\nStorage of piece and next piece overlays\nFix of wierd point system.";
			if (currupted) {
				std::cout << KRED << "BOARD WAS CURRUPTED!";
				return 0;
			}
			std::cout << std::flush;
			return 0;
		}
		read_input('d'); //Ghost piece is only generated on first input, so we make the first input automatically to generate it
		read_input('a');
		board_mutex.lock();
		print_map(true, true); //We also force a screen reprint to fix minor glitches
		board_mutex.unlock();
		while(!make_piece_fall('P', true)) {
			int ms_drop_rate = (float(float(level_to_drop_rate.at(level)) / 59.73) * 1000);
			ms_drop_rate /= difficulty+1;
			ms_sleep(ms_drop_rate);
		}
		board_mutex.lock(); //Wait untill free
		char piece_to_replace_p_with = piece_index_to_char.at(piece_on_board_index);
		auto p_cords = find_instance('P');
		for (auto it = p_cords.begin(); it != p_cords.end(); it++) {
			board[it->first][it->second] = piece_to_replace_p_with;
		}
		
		//Get a list of lines to clear
		bool line_need_clear;
		std::vector<int> lines_to_clear;
		for (int y = 23; y >= 0; y--) {
			line_need_clear = true;
			for (int x=0; x < 10; x++) {
				if (board[x][y] == ' ') {
					line_need_clear = false;
				}
			}
			if (line_need_clear) {
				line_need_clear = false;
				lines_to_clear.push_back(y);
			}
		}

		score += cleared_lines_to_score.at(lines_to_clear.size()) * level+1;
		lines_cleared += lines_to_clear.size();
		while (lines_cleared > lines_to_level_increase) {
			lines_cleared -= lines_to_level_increase;
			level += 1;
		}

		//make lines dissappear
		for (int i : lines_to_clear) {
			for (int x=0; x < 10; x++) {
				board[x][i] = 'B';
				print_map(true, false);
				ms_sleep(50);
			}
		}

		//lines flash white on tetris
		if (lines_to_clear.size() == 4) {
			for (auto b_loc : find_instance('B')) {
				board[b_loc.first][b_loc.second] = 'J';
			}
			print_map(true, false);
			ms_sleep(500);
		}

		//clear the lines (the B's)
		for (int i : lines_to_clear) {
			for (int x=0; x < 10; x++) {
				board[x][i] = ' ';
			}
		}

		//make pieces above fall
		for (int i : lines_to_clear) {
			for (int x=0; x < 10; x++) {
				for (int y=i; y < 24; y++) {
					if (board[x][y] != 'P' && board[x][y] != ' ') {
						if (!y-1 < 0) {
							board[x][y-1] = board[x][y];
						}
						board[x][y] = ' ';
					}
				}
			} 
		}
		board_mutex.unlock();
	} //Game loop

	std::cout << KRED << "ERROR: Execution should not reach here! Exited game loop?" << "\r\n";
	return 1;
}

void print_map(bool only_print_once, bool force_reprint) {
	try {
	//to avoid flicker, we will reprint only certian lines, unless the screen has too many updates, in which case we just cls.
	bool reprint_screen;
	int y_endstop = 19;
	int x_offset=2;
	int y_offset=2;
	
	while (!tetris_exit || only_print_once) {
		std::vector<std::pair<int, int> > updated_chars;
		if (!only_print_once) {
			while(board_last_state == board);
			board_mutex.lock();
		}
		for (int y=23; y > -1; y--) {
			for (int x=0; x< 10; x++) {
				if (board[x][y] != board_last_state[x][y]) {
					updated_chars.push_back(std::make_pair(x, y));
				}
			}
		}
		board_last_state = board;

		if ((updated_chars.size() > 15) || force_reprint) { //force reprint for testing
			std::cout << CLEAR_SCREEN_CODE;
			std::cout << "      Tetris++\r\n"; 
			std::cout << " ___________________\r\n";
			for (int y = y_endstop; y > -1; y--) {
				std::cout << "|";
				for (int x=0; x < 10; x++) {
					if (board[x][y] == ' ') {
						std::cout << "  ";
					} else {
						if (board[x][y] == 'P') { //P
							std::cout << piece_color.at(piece_on_board_index) << "■ " << KNRM;
						} else if (board[x][y] == 'U') { //U
							std::cout << KWHT << "O " << KNRM;
						} else if (board[x][y] == 'B') {
							std::cout << "  ";
						} else { //Everything else
							std::cout << piece_color.at(piece_char_to_index.at(board[x][y])) << "■ " << KNRM;
						}
					}
				}
				std::cout << "\b|\r\n";
			}
			std::cout << " ¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯\r\nPress \'P\' to pause\r\nthe game and view\r\ncontrols & help.";
			//std::cout << "W - Rotate\r\nL - Left\r\nR - Right\r\nS - Soft Drop\r\nSpace - Hard Drop\r\nQ - Quit\r\nP - Pause\r\n";
		} else { //partial update code. God help me
			//updated cord type std::pair<int, int>
			/*Compensate for offset
			origin is at 0,0
			spaces in board
			color*/
			//Updated cord exists on a 2d plane with 0,0 origin at the bottom left. (24, 10)
			for (auto updated_cord : updated_chars) {
				if (updated_cord.second < 20) {
					std::cout << "\x1b[" << std::abs(updated_cord.second-20)+y_offset << ";" << (updated_cord.first*2)+x_offset << "H"; //move cuorsor to pos
					if (board[updated_cord.first][updated_cord.second] == 'P') { //P
						std::cout << piece_color.at(piece_on_board_index) << "■" << KNRM;
					} else if (board[updated_cord.first][updated_cord.second] == 'U') { //U
						std::cout << KWHT << "O" << KNRM;
					} else if (board[updated_cord.first][updated_cord.second] == 'B') {
						std::cout << " ";
					} else if (board[updated_cord.first][updated_cord.second] == ' ') {
						std::cout << " ";
					} else { //Everything else
						std::cout << piece_color.at(piece_char_to_index.at(board[updated_cord.first][updated_cord.second])) << "■" << KNRM;
					}
				}
			}
		}
		std::cout << "\x1b[0;0H";
		std::cout << std::flush;
		if (!only_print_once) {
			board_mutex.unlock();
		} else {
			return; //Keep this code ricky, stop deleting it, IT BREAKS CRAP
		}
	}
	} catch (std::system_error) {
		//Cant access mutex because it is undefined. If we get here, we are trying to kill this thread anyway
		return;
	}
}

int make_piece_fall(char piece, bool lock_mutex) {
	while(paused); //Hold until not paused
	//Find all instances of P
	if (lock_mutex) {
		board_mutex.lock();
	}
	std::vector<std::pair<int, int>> p_cords = find_instance(piece);

	//Check if first piece is clear, add checks for all pieces later on
	for (std::vector<std::pair<int, int>>::iterator it = p_cords.begin(); it != p_cords.end(); it++) {
		if (it->second-1 < 0) {
			//Piece is about to move below map, and connot move any further
			if (lock_mutex) {
				board_mutex.unlock();
			}
			return 1;
		} else if (board[it->first][it->second-1] != ' ' && board[it->first][it->second-1] != 'U' && board[it->first][it->second-1] != 'P') {
			//is it a P? ony clear next condition if not P
			if (lock_mutex) {
				board_mutex.unlock();
			}
			return 1;
		}
	}

	//Move all pieces down
	for (std::vector<std::pair<int, int>>::iterator it = p_cords.begin(); it != p_cords.end(); it++) {
		board[it->first][it->second] = ' ';
		board[it->first][it->second-1] = piece;
	}
	if (lock_mutex) {
		board_mutex.unlock();
	}
	return 0;
}

void read_input(char override_input) {
	try {
	while(!tetris_exit) {
		char input;
		std::cout << "\b ";
		if (override_input == ' ') {
			input = getchar();
		}	else {
			input = override_input;
		}
		
		//Find instances of P
		std::vector<std::pair<int, int>> found_p_cords;
		std::vector<std::pair<int, int>> updated_p_cords;
		for (int x=0; x < 10; x++) {
			for (int y=0; y < 24; y++) {
				if (board[x][y] == 'P') {
					found_p_cords.push_back(std::make_pair(x, y));
				}
			}
		}
		board_mutex.lock();
		switch(input) {
			case 'w':
			case 'W':
				{
					rotation_count++;
					std::vector<std::pair<int,int>> p_cords = find_instance('P');
					std::vector<std::pair<int,int>> p_cords_from00;
					//Find smallest x and y to center falling piece over 00
					int smallest_x = 100;
					int smallest_y = 100;
					for (auto it = p_cords.begin(); it != p_cords.end(); it++) {
						if (it->first < smallest_x) {
							smallest_x = it->first;
						}
						if (it->second < smallest_y) {
							smallest_y = it->second;
						}
					}
					for (auto it = p_cords.begin(); it != p_cords.end(); it++) {
						p_cords_from00.push_back(std::make_pair(it->first-smallest_x, it->second-smallest_y));
						//std::cout << "X: " << it->first-smallest_x << ", Y: " << it->second-smallest_y << std::endl;
					}
					std::vector<std::pair<int,int>> temp;
					for (auto it = p_cords_from00.begin(); it != p_cords_from00.end(); it++) {
						temp.push_back(std::make_pair(-it->second, it->first)); //rotate vector (the magic part)
						//rotated piece = piece[-y][x]
					}
					for (auto it = temp.begin(); it != temp.end(); it++) {
						/*int i;
						if (rotation_count == 4) {
							rotation_count=0;
							i=2;
						} else {
							i=1;
						}*/
						*it = std::make_pair(((it->first)+smallest_x+1), (it->second+smallest_y)); //add numbers back to rotated vector 
					}

					if (is_space_free(temp)) { //if valid rotation, replace
						for (auto it = p_cords.begin(); it != p_cords.end(); it++) {
							board[it->first][it->second] = ' ';
						}
						for (auto it = temp.begin(); it != temp.end(); it++) {
							board[it->first][it->second] = 'P';
						}
					}
					break;
				}

			case 'a':
			case 'A':
				//move left
				//update p_cords with all the P's to the left
				for (auto it = found_p_cords.begin(); it != found_p_cords.end(); it++) {
					updated_p_cords.push_back(std::make_pair(it->first-1, it->second));
				}
				if (is_space_free(updated_p_cords)) {
					//Space is free to move over
					for (auto it = found_p_cords.begin(); it != found_p_cords.end(); it++) {
						board[it->first][it->second] = ' ';
						board[it->first-1][it->second] = 'P';
					}
				}
				break;
				
			case 'd':
			case 'D':
				//move right
				for (auto it = found_p_cords.begin(); it != found_p_cords.end(); it++) {
					updated_p_cords.push_back(std::make_pair(it->first+1, it->second));
				}
				if (is_space_free(updated_p_cords)) {
					//Space is free to move over
					for (auto it = found_p_cords.begin(); it != found_p_cords.end(); it++) {
						board[it->first][it->second] = ' ';
					}
					for (auto it = found_p_cords.begin(); it != found_p_cords.end(); it++) {
						board[it->first+1][it->second] = 'P';
					}
				}
				break;
			
			case 's':
			case 'S':
				//slow drop
				for (auto it = found_p_cords.begin(); it != found_p_cords.end(); it++) {
					updated_p_cords.push_back(std::make_pair(it->first, it->second-1));
				}
				if (is_space_free(updated_p_cords)) {
					//Space is free to move down
					for (auto it = found_p_cords.begin(); it != found_p_cords.end(); it++) {
						board[it->first][it->second] = ' ';
					}
					for (auto it = found_p_cords.begin(); it != found_p_cords.end(); it++) {
						board[it->first][it->second-1] = 'P';
					}
				}
				break;
			
			case 'p':
			case 'P':
			//TODO: Fix this, there has to be a better way of pausing the game
				paused=true;
				std::cout << CLEAR_SCREEN_CODE << "The game is " << KMAG << "Paused." << KNRM << "\r\n\nW - Rotate Piece\r\nA, D - Move Piece left and right respectively\r\nQ - Quit\r\nS - Slow drop\r\nSpace - Hard drop\r\n\n";
				std::cout << KYEL << "Note: Some new rendering code was just added and it is still somewhat buggy. Please ignore graphical glitches, they should fix themselves. If issues persist, please resize your browser window to be bigger. I am working to fix this issue, but in the end this new code will make things run alot smoother.\r\n\n" << KNRM << "Press any key to resume...";
				getchar();
				paused=false;
				print_map(true, true);
				//board_mutex.unlock();
				break;

			case 'q':
			case 'Q':
			//TODO: Properly detach threads
				system ("/bin/stty cooked");
				std::exit(0); //Close this thread
				break;

			case ' ':
				//fast drop
				while(!make_piece_fall('P', false)); //Run make piece fall until it cant fall anymore
				//Force piece to be insta locked
				/*char piece_to_replace_p_with = piece_index_to_char.at(piece_on_board_index);
				auto p_cords = find_instance('P');
				for (auto it = p_cords.begin(); it != p_cords.end(); it++) {
					board[it->first][it->second] = piece_to_replace_p_with;
				}*/
				break;

		}
		//Create representation of the block if it fell all the way down
		//Find and delete all existing U's
		for (auto cord_pair : find_instance('U')) { 
			board[cord_pair.first][cord_pair.second] = ' '; 
		}
		//Find and backup locations of P because we are overriting it
		auto locations_of_p = find_instance('P'); 

		//replace p -> u
		for (auto cord_pair : locations_of_p) { 
			board[cord_pair.first][cord_pair.second] = 'U';
		}

		//Make U fall as much as it can
		while(!make_piece_fall('U'));

		//Restore P
		for (auto cord_pair : locations_of_p) { 
			board[cord_pair.first][cord_pair.second] = 'P'; 
		}
		board_mutex.unlock();
		if (override_input != ' ') {
			return;
		}
	}
	} catch (std::system_error) {
		//Cant access mutex because it is undefined. If we get here, we are trying to kill this thread anyway
		return;
	}
}

bool spawn_piece(int override_rng) {
	std::random_device rd;
	srand(rd());
	int piece_to_spawn;
	if (override_rng == -1) {
		//ensure that the same piece doesnt show up more than 3 times per 10 blocks dropped
		if (false) {
			int occurences=0;
			do {
				piece_to_spawn = rand() % 7;
				std::cout << "Gen piece: " << piece_to_spawn;
				for (auto piece : piece_history) {
					if (piece == piece_to_spawn) { 
						occurences++; 
						std::cout << ", occur: " << occurences << "\r\n";
					}
				}
			} while (occurences >= 3);
			piece_history.erase(piece_history.begin());
		} else {
			piece_to_spawn = rand() % 7;
		}
		piece_history.push_back(piece_to_spawn);
	} else {
		piece_to_spawn = override_rng;
	}
	piece_on_board_index = piece_to_spawn;
	std::vector<std::pair<int, int>> piece_cords;
	switch(piece_to_spawn) {
		case 0:
			piece_cords = {std::make_pair(4,23), std::make_pair(4,22), std::make_pair(4,21), std::make_pair(4,20)}; //cords to spawn I
			//Line - I
			break;

		case 1:
			piece_cords = {std::make_pair(4,23), std::make_pair(5,23), std::make_pair(4,22), std::make_pair(5,22)};
			//Block - O
			break;

		case 2:
			//L piece - L
			piece_cords = {std::make_pair(4,23), std::make_pair(4,22), std::make_pair(4,21), std::make_pair(5,21)};
			break;
		
		case 3:
			//Mirror L - J
			piece_cords = {std::make_pair(5,23), std::make_pair(5,22), std::make_pair(5,21), std::make_pair(4,21)};
			break;
		
		case 4:
			//Pirymid - T
			piece_cords = {std::make_pair(3,22), std::make_pair(4,22), std::make_pair(5,22), std::make_pair(4,23)};
			break;

		case 5:
			//Up Stairs - S
			piece_cords = {std::make_pair(4,23), std::make_pair(4,22), std::make_pair(5,22), std::make_pair(5,21)};
			break;
		
		case 6:
			//Down stairs - Z
			piece_cords = {std::make_pair(5,23), std::make_pair(5,22), std::make_pair(4,22), std::make_pair(4,21)};
			break;
	}
	board_mutex.lock();
	rotation_count=0;
	if (is_space_free(piece_cords)) {
		for (auto it = piece_cords.begin(); it != piece_cords.end(); it++) {
			board[it->first][it->second] = 'P';
		}
		board_mutex.unlock();
		return true;
	} else {
		board_mutex.unlock();
		return false;
	}
}

bool is_space_free(std::vector<std::pair<int, int>> pieces) {
	for (auto it = pieces.begin(); it != pieces.end(); it++) {
		if (board[it->first][it->second] != ' ' && board[it->first][it->second] != 'P' && board[it->first][it->second] != 'U') {
			return false;
		} else if (it->first < 0 || it->first > 9) {
			return false;
		} else if (it->second < 0 || it->second > 23) {
			return false;
		}
	}
	return true;
}

std::vector<std::pair<int, int>> find_instance(char piece) {
	std::vector<std::pair<int, int>> cords;
	for (int x=0; x < 10; x++) {
		for (int y=0; y < 24; y++) {
			if (board[x][y] == piece) {
				cords.push_back(std::make_pair(x, y));
			}
		}
	}
	return cords;
}

inline void ms_sleep(int ms) {
	std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}
