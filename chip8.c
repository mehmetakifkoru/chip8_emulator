#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

#include "SDL.h"

// SDL container object
typedef struct {
	SDL_Window *window;
	SDL_Renderer *renderer;
} sdl_t;

// Emulator configuration object
typedef struct {
	uint32_t window_width;	// SDL window width
	uint32_t window_height;	// SDL window height
	uint32_t fg_color;		// Foreground color RGBA88888
	uint32_t bg_color;		// Backgro und color RGBA88888
	u_int32_t scale_factor; // Amount to scale a CHIP8 pixel by e.g. 20x will be a 20x larger window
} config_t;

// Emulator states
typedef enum {
	QUIT,
	RUNNING,
	PAUSED,
} emulator_state_t;

// CHIP8 Instruction format
typedef struct {
	uint16_t opcode;
	uint16_t NNN;		// 12 bit address/constant
	uint8_t NN;			// 8 bit constant
	uint8_t N;			// 4 bit constant
	uint8_t X;			// 4 bit register identifier
	uint8_t Y;			// 4 bit register identifier
} instruction_t;

// CHIP8 Machine Object
typedef struct {
	emulator_state_t state;
	uint8_t ram[4096];
	bool display[64*32];	// Emulate original CHIP8 resoulation pixels
	uint16_t stack[12];		// Subroutine stack
	uint16_t *stack_ptr;
	uint8_t V[16];			// Data registers V0-VF
	uint16_t I;				// Index register
	uint16_t PC;			// Program counter
	uint8_t delay_timer;	// Decrements at 60Hz when > 0
	uint8_t sound_timer;	// Decrements at 60Hz and plays tone when > 0
	bool keypad[16];		// Hexadecimal keypad 0x0-0xF
	const char *rom_name;	// Currently running ROM
	instruction_t inst;		// Currently executing instruction
} chip8_t;

// Initialize SDL	
bool init_sdl(sdl_t *sdl, const config_t config) {
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
		SDL_Log("Could not initialize SDL subsystems! %s\n", SDL_GetError());
		return false;
	}

	sdl->window = SDL_CreateWindow("CHIP8 Emulator", SDL_WINDOWPOS_CENTERED,
								   SDL_WINDOWPOS_CENTERED,
								   config.window_width * config.scale_factor,
								   config.window_height * config.scale_factor, 
								   0);
								   	
	if(!sdl->window) {
		SDL_Log("Could not create SDL window %s\n", SDL_GetError());
		return false;
	}

	sdl->renderer = SDL_CreateRenderer(sdl->window, -1, SDL_RENDERER_ACCELERATED);
	if(!sdl->renderer) {
	SDL_Log("Could not create SDL renderer %s\n", SDL_GetError());
	return false;
	}

	return true;	//Success
}

// Set up initial emulator configuration from passed in arguments
bool set_config_from_args(config_t *config, const int argc, char **argv) {
	// Set defaults
	*config = (config_t){
		.window_width = 64,		// CHIP8 original X resoulation
		.window_height = 32,	// CHIP8 original Y resoulation
		.fg_color = 0xFFFFFFFF, // White
    	.bg_color = 0x31313131, // Black
		.scale_factor = 20,		// Default resoluation will be 1280x640
	};

	// Override defaults from passed in arguments
	for(int i=1; i < argc; i++) {
		(void)argv[i];	// Prevent compiler error from unused variables argc/argv
		// ...
	}

	return true; // Success
}

bool init_chip8(chip8_t *chip8, const char *rom_name) {
	const uint32_t entry_point = 0x200;		// CHIP8 ROMs willl be loaded to 0x200
	const uint8_t font[] = {
		0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
		0x20, 0x60, 0x20, 0x20, 0x70, // 1
		0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
		0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
		0x90, 0x90, 0xF0, 0x10, 0x10, // 4
		0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
		0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
		0xF0, 0x10, 0x20, 0x40, 0x40, // 7
		0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
		0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
		0xF0, 0x90, 0xF0, 0x90, 0x90, // A
		0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
		0xF0, 0x80, 0x80, 0x80, 0xF0, // C
		0xE0, 0x90, 0x90, 0x90, 0xE0, // D
		0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
		0xF0, 0x80, 0xF0, 0x80, 0x80  // F
	};
	// Load Font
	memcpy(&chip8->ram[0], font, sizeof(font));

	// Open ROM file
	FILE *rom = fopen(rom_name, "rb");
	if(!rom) {
		SDL_Log("Rom file %s is invalid or does not exist\n", rom_name);
		return false;
	}

	// Get/check rom size
	fseek(rom, 0, SEEK_END);
	const size_t rom_size = ftell(rom);
	const size_t max_size = sizeof chip8->ram - entry_point;
	rewind(rom);

	if(rom_size > max_size) {
		SDL_Log("Rom file %s is too big! ROM size: %zu, MAX size allowed: %zu\n", rom_name, rom_size, max_size);
		return false;
	}

	// Load ROM
	if(fread(&chip8->ram[entry_point], rom_size, 1, rom) != 1) {
		SDL_Log("Could not read ROM file %s into CHIP8 memory\n", rom_name);
		return false;
	}

	fclose(rom);

	// Set chip8 machine defaults
	chip8->state = RUNNING; 				// Default machine state to on/running
	chip8->PC = entry_point;				// Start program counter at ROM entry point
	chip8->rom_name = rom_name;	
	chip8->stack_ptr = &chip8->stack[0];

	return true; // Success
}

// Final cleanup
void final_cleanup(const sdl_t sdl) {
	SDL_DestroyRenderer(sdl.renderer);	
	SDL_DestroyWindow(sdl.window);
	SDL_Quit(); // Shutdown SDL subsystem
}

// Clear Screen / SDL Window to background color
void clear_screen(const sdl_t sdl, config_t config) {
	const uint8_t r = (config.bg_color >> 24) & 0xFF;
	const uint8_t g = (config.bg_color >> 16) & 0xFF;
	const uint8_t b = (config.bg_color >>  8) & 0xFF;
	const uint8_t a = (config.bg_color >>  0) & 0xFF;
	SDL_SetRenderDrawColor(sdl.renderer, r, g, b, a);
	SDL_RenderClear(sdl.renderer);
}

// Update window with any changes
void update_screen(const sdl_t sdl) {
	SDL_RenderPresent(sdl.renderer);
}

void handle_input(chip8_t *chip8) {
	SDL_Event event;
	while(SDL_PollEvent(&event)) {
		switch (event.type) {
			case SDL_QUIT:
			// Exit window; End program
				chip8->state = QUIT; // Will exit main emulator
				return;
			case SDL_KEYDOWN:
				switch (event.key.keysym.sym) {
					case SDLK_ESCAPE:
					// Escape key; Exit window & End program
					chip8->state = QUIT;
					return;
					case SDLK_SPACE:
						// Space Bar
						if(chip8->state == RUNNING) {
							chip8->state = PAUSED;		// Pause
						}
						else {
							chip8->state = RUNNING; 	// Resume
							puts("==== RESUME ====");
						}
				default:
					break;
				}
				break;
			case SDL_KEYUP:
				break;
			default:
				break;
		}
	}
}

// Emulate 1 CHIP8 instruction
void emulate_instruction(chip8_t *chip8) {
	// Get next opcode from ram
	chip8->inst.opcode = (chip8->ram[chip8->PC] << 8) | chip8->ram[chip8->PC+1];
	chip8->PC += 2;	// Pre-increament program counter for next opcode

	// Fill out current instruction format
	chip8->inst.NNN = chip8->inst.opcode & 0x0FFF;
	chip8->inst.NN = chip8->inst.opcode & 0x0FF;
	chip8->inst.N = chip8->inst.opcode & 0x0F;
	chip8->inst.X = (chip8->inst.opcode >> 8) & 0x0F;
	chip8->inst.Y = (chip8->inst.opcode >> 4) & 0x0F;

	// Emulate opcode
	switch((chip8->inst.opcode >> 12) & 0x0F) {
		case 0x00:
			if(chip8->inst.NN == 0xE0) {
				// 0x00E0: Clear the screen
				memset(&chip8->display[0], false, sizeof chip8->display);
			} else if(chip8->inst.NN == 0xEE) {
				// 0x00EE: Return from subroutine
				// Set program counter to last address on subroutine stack ("pop" it off the stack)
				// so that next opcode will be gotten from that address. 
				chip8->PC = *--chip8->stack_ptr; 
			}
			break;
		case 0x02:
			// 0x2NNN: Call subroutine at NNN
			// Store current address to return to on subroutine stack ("push" it on the stack)
			// and set program counter to subroutine address so that the next opcode 
			// is gotten from there.
			*chip8->stack_ptr++ = chip8->PC;
			chip8->PC = chip8->inst.NNN;
			break;
		default:
			break;	// Unimplemented or invalid opcode
	}
}

// Da main squeeze
int main(int argc, char **argv) {
	// Default Usage message for args
	if(argc < 2) {
		fprintf(stderr, "Usage: %s <rom_name>\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	// Initialize Emulator configuration/options
	config_t config = {0};
	if(!set_config_from_args(&config, argc, argv)) exit(EXIT_FAILURE);
	
	// Initialize SDL
	sdl_t sdl = {0};
	if(!init_sdl(&sdl, config)) exit(EXIT_FAILURE); 

	// Initialize CHIP8 machine
	chip8_t chip8 = {0};
	const char *rom_name = argv[1];
	if(!init_chip8(&chip8, rom_name)) exit(EXIT_FAILURE); 

	// Initial screen clear to background color
	clear_screen(sdl, config);

	// Main Emulator loop
	while(chip8.state != QUIT) {
		// Handle user input
		handle_input(&chip8);

		if(chip8.state == PAUSED) continue;

		// Get_time();

		// Emulate CHIP8 Instructions
		emulate_instruction(&chip8);
		
		// Get_timr() elapsed since last get_time();
		// Delay for approximately 60hz/60fps (16.67ms)
		// SDL_Delay(16 - actual time elapsed);
		SDL_Delay(16);

		// Update window with changes
		update_screen(sdl);
	}
	// Final cleanup
	final_cleanup(sdl);

	exit(EXIT_SUCCESS);
}

