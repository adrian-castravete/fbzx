/*
 * Copyright 2003-2015 (C) Raster Software Vigo (Sergio Costas)
 * This file is part of FBZX
 *
 * FBZX is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * FBZX is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

/*
 * tape.cpp
 *
 *  Created on: 15/03/2015
 *      Author: raster
 */

#include "tape.hh"
#include "z80free/Z80free.h"

#include <stdio.h>
#include <string.h>

class TapeBlock {

private:
	class TapeBlock *next;
	uint8_t signal;
protected:
	uint32_t counter0;
	uint32_t counter1;
	bool zero_first;
public:

	TapeBlock() {
		this->next = NULL;
		this->signal = 0;
		this->counter0 = 0;
		this->counter1 = 0;
		this->zero_first = true;
	}

	virtual ~TapeBlock() {

		if (this->next != NULL) {
			delete (this->next);
		}
	}

	virtual bool fast_load(uint8_t *data, uint16_t &size, uint8_t &flag) {
		size = 0;
		flag = 0;
		return false;
	}

	/**
	 * Adds a new block to the end of the list
	 * @param block The block to add to the list
	 */
	void add_block(class TapeBlock *new_block) {

		if (this->next == NULL) {
			this->next = new_block;
		} else {
			this->next->add_block(new_block);
		}
	}

	/**
	 * Returns the next block in the list
	 * @return The block next to this one
	 */
	class TapeBlock *next_block() {
		return this->next;
	}

	/**
	 * Returns the tape signal at the current instant
	 * @return Tape signal
	 */
	uint8_t read_signal() {

		return this->signal;
	}

	/**
	 * Emulates the block for the specified time
	 * @param tstates The number of tstates to emulate
	 * @return The number of tstates that remained to emulate because the block ended prematurely
	 */
	uint32_t play(uint32_t tstates) {

		if (this->zero_first) {
			if (this->counter0 > 0) {
				if (this->counter0 >= tstates) {
					this->counter0 -= tstates;
					this->signal = 0;
					return 0;
				} else {
					tstates -= this->counter0;
					this->counter0 = 0;
				}
			}
			if (this->counter1 > 0) {
				if (this->counter1 >= tstates) {
					this->counter1 -= tstates;
					this->signal = 1;
					return 0;
				} else {
					tstates -= this->counter1;
					this->counter1 = 0;
				}
			}
			return tstates;
		} else {
			if (this->counter1 > 0) {
				if (this->counter1 >= tstates) {
					this->counter1 -= tstates;
					this->signal = 1;
					return 0;
				} else {
					tstates -= this->counter1;
					this->counter1 = 0;
				}
			}
			if (this->counter0 > 0) {
				if (this->counter0 >= tstates) {
					this->counter0 -= tstates;
					this->signal = 0;
					return 0;
				} else {
					tstates -= this->counter0;
					this->counter0 = 0;
				}
			}
			return tstates;
		}
	}

	/**
	 * Sets the time values for the next bit to be read from the tape
	 * @return TRUE if there is a new bit; FALSE if there are no more bits in this block
	 */
	virtual bool next_bit() = 0;

	/**
	 * Resets a block to its initial state. Mandatory before starting working with it
	 */
	virtual void reset() {
	}
};

class FullBlock : public TapeBlock {

	uint8_t *data;
	uint32_t data_size;
	uint16_t pilot;
	uint16_t sync0;
	uint16_t sync1;
	uint16_t zero;
	uint16_t one;
	uint16_t lpilot;
	uint8_t bits_at_end;
	uint32_t pause;

	uint8_t bit;
	uint8_t bit_counter;
	uint32_t loop;
	bool allow_fast_load;

	int pointer;
	enum TURBOBLOCK_states {TURBOBLOCK_GUIDE, TURBOBLOCK_DATA, TURBOBLOCK_PAUSE} current_state;

	void set_state(enum TURBOBLOCK_states new_state) {

		this->current_state = new_state;
		switch (new_state) {
		case TURBOBLOCK_GUIDE:
			this->loop = this->lpilot/2;
		break;
		case TURBOBLOCK_DATA:
			this->pointer = 0;
			this->bit = 0x80;
			if (this->data_size == 1) {
				this->bit_counter = this->bits_at_end;
			} else {
				this->bit_counter = 8;
			}
		break;
		case TURBOBLOCK_PAUSE:
		break;
		}
	}

public:

	FullBlock(uint8_t *data,uint32_t size, uint16_t pause) :TapeBlock() {

		this->allow_fast_load = true;
		this->data = new uint8_t[size];
		this->data_size = size;
		this->pilot = 2168;
		this->sync0 = 667;
		this->sync1 = 735;
		this->zero = 855;
		this->one = 1710;
		if (!(0x80 & data[0])) {
			this->lpilot = 8063; // 5 seconds (aprox)
		} else {
			this->lpilot = 3223; // 2 seconds (aprox)
		}
		this->bits_at_end = 8;
		this->pause = (uint32_t)pause;

		memcpy(this->data,data,size);
		this->current_state = TURBOBLOCK_GUIDE;
		this->loop = this->lpilot;
		this->pointer = 0;
		this->bit = 0x80;
		this->bit_counter = 8;
		this->set_state(TURBOBLOCK_GUIDE);
	}

	FullBlock(uint8_t *data,uint32_t size, uint16_t pilot, uint16_t sync0, uint16_t sync1, uint16_t zero, uint16_t one, uint16_t lpilot, uint8_t bits_at_end, uint16_t pause) :TapeBlock() {

		this->allow_fast_load = false;
		this->data = new uint8_t[size];
		this->data_size = size;
		this->pilot = pilot;
		this->sync0 = sync0;
		this->sync1 = sync1;
		this->zero = zero;
		this->one = one;
		this->lpilot = lpilot;
		this->bits_at_end = bits_at_end;
		this->pause = (uint32_t)pause;

		memcpy(this->data,data,size);
		this->current_state = TURBOBLOCK_GUIDE;
		this->loop = lpilot;
		this->pointer = 0;
		this->bit = 0x80;
		this->bit_counter = 8;
		this->set_state(TURBOBLOCK_GUIDE);
	}

	void reset() {
		this->counter0 = 0;
		this->counter1 = 0;
		this->set_state(TURBOBLOCK_GUIDE);
	}

	bool next_bit() {

		bool current_bit;

		switch (this->current_state) {
		case TURBOBLOCK_GUIDE:
			if (this->loop > 0) {
				// guide tone loop
				this->counter0 = this->pilot;
				this->counter1 = this->pilot;
				this->loop--;
			} else {
				// sync bit
				this->counter0 = this->sync0;
				this->counter1 = this->sync1;
				this->set_state(TURBOBLOCK_DATA);
			}
			return true;
		break;
		case TURBOBLOCK_DATA:
			if (this->bit_counter == 0) {
				this->bit = 0x80;
				this->pointer++;
				if ((this->data_size-1) == this->pointer) {
					this->bit_counter = this->bits_at_end;
				} else {
					this->bit_counter = 8;
				}
				if (this->pointer == this->data_size) {
					this->counter0 = this->pause * 3500;
					this->counter1 = 0;
					this->set_state(TURBOBLOCK_PAUSE);
					return true;
				}
			}
			current_bit = ((*(this->data+this->pointer)) & this->bit) == 0;
			if (current_bit) {
				this->counter0 = this->zero;
				this->counter1 = this->zero;
			} else {
				this->counter0 = this->one;
				this->counter1 = this->one;
			}
			this->bit /=2;
			this->bit_counter--;
			return true;
		break;
		case TURBOBLOCK_PAUSE:
			return false; // end of data
		break;
		}
		return true;
	}

	bool fast_load(uint8_t *data, uint16_t &size, uint8_t &flag) {
		if (this->allow_fast_load) {
			flag = this->data[0];
			size = this->data_size - 2;
			memcpy(data,this->data+1,size);
			return true;
		} else {
			return false;
		}
	}
};

Tape::Tape() {
	this->blocks = NULL;
	this->current_block = NULL;
	this->paused = true;
	this->block_accesed = false;
}

Tape::~Tape() {
	this->delete_blocks();
}

void Tape::add_block(class TapeBlock *new_block) {

	if (this->blocks == NULL) {
		this->blocks = new_block;
	} else {
		this->blocks->add_block(new_block);
	}
}

void Tape::delete_blocks() {

	delete (this->blocks);
	this->blocks = NULL;
	this->current_block = NULL;
	this->block_accesed = false;
}

bool Tape::load_file(char *filename) {
	char char_id[10];
	int retval;
	this->delete_blocks();

	this->paused = true;
	FILE *file = fopen(filename,"rb");
	if (file == NULL) {
		return true; // error while opening the file
	}
	retval=fread(char_id,10,1,file); // read the (maybe) TZX header
	fclose(file);
	if((!strncmp(char_id,"ZXTape!",7)) && (char_id[7]==0x1A)&&(char_id[8]==1)) {
		return this->load_tzx(filename);
	} else {
		return this->load_tap(filename);
	}
}

bool Tape::read_8bit(FILE *file, uint8_t &value) {

	uint8_t data;
	size_t retval;

	retval = fread (&data, 1, 1, file);
	if (retval < 1) {
		fclose(file); // end-of-file
		return true;
	}
	value = data;
	return false;
}

bool Tape::read_16bit(FILE *file, uint16_t &value) {

	uint8_t data[2];
	size_t retval;

	retval = fread (data, 2, 1, file);
	if (retval < 1) {
		fclose(file); // end-of-file
		return true;
	}
	value = ((uint16_t) data[0]) + 256 * ((uint16_t) data[1]);
	return false;
}

bool Tape::read_24bit(FILE *file, uint32_t &value) {

	uint8_t data[3];
	size_t retval;

	retval = fread (data, 3, 1, file);
	if (retval < 1) {
		fclose(file); // end-of-file
		return true;
	}
	value = ((uint32_t) data[0]) + 256 * ((uint32_t) data[1]) + 65536 * ((uint32_t) data[2]);
	return false;
}

bool Tape::load_tap(char *filename) {

	FILE *file;
	uint8_t data[65536];
	uint16_t size;
	size_t retval;

	file = fopen(filename,"rb");
	if (file == NULL) {
		return true; // error while opening the file
	}
	do {
		// read block size
		if (this->read_16bit(file,size)) {
			return true;
		}
		retval = fread (data, size, 1, file);
		if (retval != 1) {
			fclose(file);
			return (true); // end-of-file and error
		}
		this->add_block(new FullBlock(data,size,1000));
	} while(true);

	fclose(file);
	this->current_block = this->blocks;
	return false;
}

bool Tape::load_tzx(char *filename) {

	FILE *file;
	uint8_t block_type;
	uint8_t tmpdata[10];
	size_t retval;

	file = fopen(filename,"rb");
	if (file == NULL) {
		return true; // error while opening the file
	}
	fread(tmpdata,10,1,file); // read TZX header
	while(true) {
		// read block type
		retval = fread (&block_type, 1, 1, file);
		if (retval < 1) {
			break; // end-of-file
		}
		switch(block_type) {
		case 0x10: // standard block
			if (this->tzx_standard_block(file)) {
				return true;
			}
		break;
		case 0x11: // turboblock
			if (this->tzx_turbo_block(file)) {
				return true;
			}
		break;
		default:
			printf("Block unknown: %X\n",block_type);
			return true;
		break;
		}
	}

	fclose(file);
	this->current_block = this->blocks;
	return false;
}

bool Tape::tzx_standard_block(FILE *file) {

	uint8_t data[65536];
	uint16_t size;
	uint16_t pause;
	size_t retval;

	// read pause duration
	if (this->read_16bit(file,pause)) {
		return true;
	}
	// read block size
	if (this->read_16bit(file,size)) {
		return true;
	}
	retval = fread (data, size, 1, file);
	if (retval != 1) {
		fclose(file);
		return (true); // end-of-file and error
	}
	this->add_block(new FullBlock(data,size,pause));
	return false;
}

bool Tape::tzx_turbo_block(FILE *file) {

	uint8_t *data;
	uint32_t size;
	uint16_t pause;
	uint16_t pilot;
	uint16_t sync0;
	uint16_t sync1;
	uint16_t zero;
	uint16_t one;
	uint16_t lpilot;
	uint8_t bits_at_end;
	size_t retval;

	// read pilot pulse duration
	if (this->read_16bit(file,pilot)) {
		return true;
	}
	// read sync duration
	if (this->read_16bit(file,sync0)) {
		return true;
	}
	if (this->read_16bit(file,sync1)) {
		return true;
	}
	// read bit duration
	if (this->read_16bit(file,zero)) {
		return true;
	}
	if (this->read_16bit(file,one)) {
		return true;
	}
	// read pilot duration
	if (this->read_16bit(file,lpilot)) {
		return true;
	}
	// read number of bits at end
	if (this->read_8bit(file,bits_at_end)) {
		return true;
	}
	// read pause duration
	if (this->read_16bit(file,pause)) {
		return true;
	}
	// read block size
	if (this->read_24bit(file,size)) {
		return true;
	}
	data = new uint8_t[size];
	retval = fread (data, size, 1, file);
	if (retval != 1) {
		fclose(file);
		return (true); // end-of-file and error
	}
	this->add_block(new FullBlock(data,size,pilot,sync0,sync1,zero,one,lpilot,bits_at_end,pause));
	return false;
}

void Tape::play(uint32_t tstates) {

	uint32_t residue = tstates;

	if (this->current_block == NULL) {
		return;
	}

	if (this->paused) {
		return;
	}

	this->block_accesed = true;
	while(true) {
		residue = this->current_block->play(residue);
		if (residue != 0) {
			if (this->current_block->next_bit() == false) {
				this->current_block = this->current_block->next_block();
				if (this->current_block == NULL) {
					this->current_block = this->blocks;
				}
				this->current_block->reset();
				this->block_accesed = false;
			}
		} else {
			break;
		}
	}
}

uint8_t Tape::read_signal() {

	if (this->current_block == NULL) {
		return 0;
	} else {
		return this->current_block->read_signal();
	}
}

void Tape::rewind() {
	this->current_block = this->blocks;
	if (this->current_block != NULL) {
		this->current_block->reset();
	}
	this->block_accesed = false;
}

void Tape::set_pause(bool pause) {
	this->paused = pause;
}

enum FastLoadReturn Tape::fast_read(uint8_t *data, uint16_t &length,uint8_t flag) {

	uint8_t block_flag;

	if (this->blocks == NULL) {
		return FASTLOAD_NO_TAPE;
	}

	if (this->block_accesed) {
		if (this->current_block != NULL) {
			this->current_block = this->current_block->next_block();
		}
	}
	if (this->current_block == NULL) {
		return FASTLOAD_END_TAPE;
	}
	this->block_accesed = false;

	if (!this->current_block->fast_load(data,length,block_flag)) {
		return FASTLOAD_NO_BLOCK;
	}
	this->current_block = this->current_block->next_block();
	if (this->current_block != NULL) {
		this->current_block->reset();
	}

	printf("Cargo bloque de longitud %d\n",length);
	if (block_flag != flag) {
		return FASTLOAD_NO_FLAG;
	}

	return FASTLOAD_OK;
}
