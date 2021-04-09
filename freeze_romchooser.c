/*
  Rom chooser for freeze menu.

  It is displayed over the top of the normal freeze menu,
  and so we use that screen mode.

  We get our list of rom nanes and put them at $40000.
  As we only care about their names, and file names are
  limited to 64 characters, we can fit ~1000.
  In fact, we can only safely mount images with names <32
  characters.

  We return the disk image name or a NULL pointer if the
  selection has failed and $FFFF if the user cancels selection
  of a disk.
*/


#include <stdio.h>
#include <string.h>

#include "freezer.h"
#include "fdisk_hal.h"
#include "fdisk_memory.h"
#include "fdisk_screen.h"
#include "fdisk_fat32.h"
#include "ascii.h"


short file_count=0;
short selection_number=0;
short display_offset=0;

char *reading_disk_list_message="SCANNING DIRECTORY ...";

char *diskchooser_instructions=
  "  SELECT ROM IMAGE, THEN PRESS RETURN  "
  "  OR PRESS RUN/STOP TO LEAVE UNCHANGED  ";

unsigned char normal_row[40]={
  0,1,0,1,0,1,0,1,
  0,1,0,1,0,1,0,1,
  0,1,0,1,0,1,0,1,
  0,1,0,1,0,1,0,1,
  0,1,0,1,0,1,0,1
};

unsigned char highlight_row[40]={
  0,0x21,0,0x21,0,0x21,0,0x21,0,0x21,0,0x21,0,0x21,0,0x21,
  0,0x21,0,0x21,0,0x21,0,0x21,0,0x21,0,0x21,0,0x21,0,0x21,
  0,0x21,0,0x21,0,0x21,0,0x21
};

unsigned char dir_line_colour[40]={
  0,0xe,0,0xe,0,0xe,0,0xe,0,0xe,0,0xe,0,0xe,0,0xe,
  0,0xe,0,0xe,0,0xe,0,0xe,0,0xe,0,0xe,0,0xe,0,0xe,
  0,0xe,0,0xe,0,0xe,0,0xe
};

char disk_name_return[32];

unsigned char joy_to_key_disk[32]={
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0x0d, // With fire pressed
  0,0,0,0,0,0,0,0x9d,0,0,0,0x1d,0,0x11,0x91,0     // without fire
};

int load_rom(char* romName) {
  long freezeAddr = 0x20000L;
  long romAddr = 0x40000L;


  //load rom image to 40000-5ffff
  read_file_from_sdcard(romName, 0x40000L);

  //copy 40000-5ffff to freeze-memory 20000-3ffff
  for (;romAddr < 0x5ffff; romAddr += 256, freezeAddr+=256) {
    //copy rom to freeze address
    freeze_load_page(freezeAddr, (unsigned char*)(romAddr));
  }

  return 0;
}

void draw_disk_image_list(void)
{
  unsigned addr=SCREEN_ADDRESS;
  unsigned char i,x;
  unsigned char name[64];
  // First, clear the screen
  POKE(SCREEN_ADDRESS+0,' ');
  POKE(SCREEN_ADDRESS+1,0);
  POKE(SCREEN_ADDRESS+2,' ');
  POKE(SCREEN_ADDRESS+3,0);
  lcopy(SCREEN_ADDRESS,SCREEN_ADDRESS+4,40*2*23-4);
  lpoke(COLOUR_RAM_ADDRESS+0,0);
  lpoke(COLOUR_RAM_ADDRESS+1,1);
  lpoke(COLOUR_RAM_ADDRESS+2,0);
  lpoke(COLOUR_RAM_ADDRESS+3,1);
  lcopy(COLOUR_RAM_ADDRESS,COLOUR_RAM_ADDRESS+4,40*2*23-4);

  // Draw instructions
  for(i=0;i<80;i++) {
    if (diskchooser_instructions[i]>='A'&&diskchooser_instructions[i]<='Z') 
      POKE(SCREEN_ADDRESS+23*80+(i<<1)+0,diskchooser_instructions[i]&0x1f);
    else
      POKE(SCREEN_ADDRESS+23*80+(i<<1)+0,diskchooser_instructions[i]);
    POKE(SCREEN_ADDRESS+23*80+(i<<1)+1,0);
  }
  lcopy((long)highlight_row,COLOUR_RAM_ADDRESS+(23*80)+0,40);
  lcopy((long)highlight_row,COLOUR_RAM_ADDRESS+(23*80)+40,40);
  lcopy((long)highlight_row,COLOUR_RAM_ADDRESS+(24*80)+0,40);
  lcopy((long)highlight_row,COLOUR_RAM_ADDRESS+(24*80)+40,40);

  
  for(i=0;i<23;i++) {
    if ((display_offset+i)<file_count) {
      // Real line
      lcopy(0x40000U+((display_offset+i)<<6),(unsigned long)name,64);

      for(x=0;x<20;x++) {
	if ((name[x]>='A'&&name[x]<='Z') ||(name[x]>='a'&&name[x]<='z'))
	  POKE(addr+(x<<1),name[x]&0x1f);
	else
	  POKE(addr+(x<<1),name[x]);
      }
    } else {
      // Blank dummy entry
      for(x=0;x<40;x++) POKE(addr+(x<<1),' ');
    }
    if ((display_offset+i)==selection_number) {
      // Highlight the row
      lcopy((long)highlight_row,COLOUR_RAM_ADDRESS+(i*80),40);
    } else {
      // Normal row
      lcopy((long)normal_row,COLOUR_RAM_ADDRESS+(i*80),40);
    }
    addr+=(40*2);  
  }

  
}

void scan_directory(void)
{
  unsigned char x,dir;
  struct m65_dirent *dirent;

  file_count=0;

  closeall();
  
  dir=opendir();
  dirent=readdir(dir);
  while(dirent&&((unsigned short)dirent!=0xffffU)) {

    x=strlen(dirent->d_name);

    // check DIR attribute of dirent
    if (dirent->d_type&0x10) {

      // File is a directory
      if (x<60) {
	lfill(0x40000L+(file_count*64),' ',64);
	lcopy((long)&dirent->d_name[0],0x40000L+1+(file_count*64),x);
	// Put / at the start of directory names to make them obviously different
	lpoke(0x40000L+(file_count*64),'/');
	// Don't list "." directory pointer
	if (strcmp(".",dirent->d_name)) 
	  file_count++;
      }
    }
    else if (x>4) {
      if ((!strncmp(&dirent->d_name[x-4],".BIN",4))||(!strncmp(&dirent->d_name[x-4],".bin",4))) {
	// File is a disk image
	lfill(0x40000L+(file_count*64),' ',64);
	lcopy((long)&dirent->d_name[0],0x40000L+(file_count*64),x);
	file_count++;
      }
    }
    
    dirent=readdir(dir);
  }

  closedir(dir);
}

char *freeze_select_rom_image(void)
{
  unsigned char x;
  int idle_time=0;
  
  file_count=0;
  selection_number=0;
  display_offset=0;
  
  // First, clear the screen
  POKE(SCREEN_ADDRESS+0,' ');
  POKE(SCREEN_ADDRESS+1,0);
  POKE(SCREEN_ADDRESS+2,' ');
  POKE(SCREEN_ADDRESS+3,0);
  lcopy(SCREEN_ADDRESS,SCREEN_ADDRESS+4,40*2*25-4);

  for(x=0;reading_disk_list_message[x];x++)
    POKE(SCREEN_ADDRESS+12*40*2+(9*2)+(x*2),reading_disk_list_message[x]&0x3f);
  
  scan_directory();
  
  // If we didn't find any disk images, then just return
  if (!file_count) return NULL;

  // Okay, we have some disk images, now get the user to pick one!
  draw_disk_image_list();
  while(1) {
    x=PEEK(0xD610U);

    if(!x) {
      // We use a simple lookup table to do this
      x=joy_to_key_disk[PEEK(0xDC00)&PEEK(0xDC01)&0x1f];
      // Then wait for joystick to release
      while((PEEK(0xDC00)&PEEK(0xDC01)&0x1f)!=0x1f) continue;
    }
    
    // Clear read key
    POKE(0xD610U,0);

    switch(x) {
    case 0x03:             // RUN-STOP = make no change
      return NULL;
    case 0x5f: // <- key at top left of key board
      // Go back up one directory
      mega65_dos_chdir("..");
      file_count=0;
      selection_number=0;
      display_offset=0;
      scan_directory();
      draw_disk_image_list();
      
      break;
    case 0x0d: case 0x21:            // Return = select this disk.
      // Copy name out
      lcopy(0x40000L+(selection_number*64),(unsigned long)disk_name_return,32);
      // Then null terminate it
      for(x=31;x;x--)
	      if (disk_name_return[x]==' ') { disk_name_return[x]=0; } else { break; }
      
      // Try to mount it, with border black while working
      POKE(0xD020U,0);
      if (disk_name_return[0]=='/') {
      	// Its a directory
      	mega65_dos_chdir(&disk_name_return[1]);
      	file_count=0;
      	selection_number=0;
      	display_offset=0;
      	scan_directory();
      	draw_disk_image_list();
      } else {
      	
    	  if (load_rom(disk_name_return)) {
    	    // Mounting the image failed
    	    POKE(0xD020U,2);
    	    
    	    break;
    	  }
      	
      	POKE(0xD020U,6);
      	
      	// Mounted ok, so return this image
      	return disk_name_return;
      }
      break;
    case 0x11: case 0x9d:  // Cursor down or left
      POKE(0xD020U,6);
      selection_number++;
      if (selection_number>=file_count) selection_number=0;
      break;
    case 0x91: case 0x1d:  // Cursor up or right
      POKE(0xD020U,6);
      selection_number--;
      if (selection_number<0) selection_number=file_count-1;
      break;
    }

    // Adjust display position
    if (selection_number<display_offset) display_offset=selection_number;
    if (selection_number>(display_offset+22)) display_offset=selection_number-22;
    if (display_offset>(file_count-22)) display_offset=file_count-22;
    if (display_offset<0) display_offset=0;

    draw_disk_image_list();
    
  }
  
  return NULL;
}
