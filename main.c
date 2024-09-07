/*
	NIOS program

	This design uses the scatter gather DMA (SGDMA) to transfer data
	from memory to hardware accelerator and other way around.

	This design example performs the following transfers:
	SSRAM(MM) --> (MM)SGDMA(ST) --> (ST)ACCELERATOR(ST) --> (ST)SGDMA(MM) --> SSRAM(MM)

	Two SGDMA components are used in this system since Avalon-ST is a
	uni-directional point to point method of connecting IP.
*/

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "alt_types.h"
#include "altera_avalon_performance_counter.h"
#include "altera_avalon_sgdma.h"
#include "altera_avalon_sgdma_regs.h"
#include "sys/alt_cache.h"
#include "system.h"

// set to greater than 0 for extensive printf during operation
#define VERBOSE_LEVEL 0

// set to greater than 0 for writing output data to files
#define WRITE_OUTPUTS_TO_FILE 0

// filename lenght limits
#define INPUT_FILENAME_MAX_LEN 	20
#define OUTPUT_FILENAME_MAX_LEN 40

// output filename templates
#define OUTPUT_FILENAME_SW "/mnt/host/output/out_sw"
#define OUTPUT_FILENAME_HW "/mnt/host/output/out_hw"

// scaling factor range / limits
#define SCALING_FACTOR_MIN 1
#define SCALING_FACTOR_MAX 4

// some useful constants
#define DESCRIPTOR_BUFFER_LEN_MAX 65535

#define BIGGEST_32BIT_UNSIGNED_NUMBER 4294967295
#define BIGGEST_16BIT_UNSIGNED_NUMBER 65535

// HW related constants
#define ADDR_WIDTH_0 	0x0
#define ADDR_WIDTH_1 	0x1
#define ADDR_WIDTH_2 	0x2
#define ADDR_WIDTH_3 	0x3
#define ADDR_HEIGHT_0 	0x4
#define ADDR_HEIGHT_1 	0x5
#define ADDR_HEIGHT_2 	0x6
#define ADDR_HEIGHT_3 	0x7
#define ADDR_STATUS 	0x8
#define ADDR_CONTROL 	0x9

#define BIT_CONTROL_RESET 		0x80
#define BIT_CONTROL_START 		0x40
#define BIT_CONTROL_INCREASE 	0x20

// typedefs
typedef enum { SF1=SCALING_FACTOR_MIN, SF2, SF3, SF4 } ScalingFactor_t;

typedef enum { DECREASE, INCREASE } IncreaseDecreaseResolution_t;

typedef enum { WHOLE, PART } PartOfImageToProcess_t;

typedef struct {
	PartOfImageToProcess_t whole_part;
	alt_u32 row;
	alt_u32 col;
	alt_u32 width;
	alt_u32 height;
} ImagePartParameters_t;

typedef struct {
	alt_u32 width;
	alt_u32 height;
	alt_u8 **pixels;
} Image_t ;

/*
	------------------------------------------------------------------------------------------------
	parses user input

	parse user inputted: input filename, scaling factor and increase/decrease
	------------------------------------------------------------------------------------------------
*/
alt_u32 initialUserInput(alt_8 *input_filename, ScalingFactor_t *scaling_factor, IncreaseDecreaseResolution_t *increase_decrease) {
    // user input parsing
    alt_u32 i;
    alt_32 c;

    printf("{input filename} {scaling factor} {increase/decrease}\n");
    printf("                      [%u,%u]               %u/%u\n", SCALING_FACTOR_MIN, SCALING_FACTOR_MAX, INCREASE, DECREASE);

    // read input filename until maximum alowed len or until space char
    for(i = 0; i < INPUT_FILENAME_MAX_LEN-1 && (c = getchar()) != ' '; i++) {
        input_filename[i] = c;
    }
    // insert '\0' character at the end of the input filename
    input_filename[i] = '\0';
    if (i == INPUT_FILENAME_MAX_LEN-1) {
        printf("ERROR: Input filename exceeded maximum alowed lenght of %d characters\n", INPUT_FILENAME_MAX_LEN);
        while(getchar() != '\n');
        return 1;
    }

    // read scaling factor
    c = getchar() - '0';
    if (c < SCALING_FACTOR_MIN || c > SCALING_FACTOR_MAX) {
        printf("ERROR: Scaling factor must be a number in range [%d,%d]\n", SCALING_FACTOR_MIN, SCALING_FACTOR_MAX);
        while(getchar() != '\n');
        return 1;
    }
    *scaling_factor = c;

    // skip space after scaling factor
    getchar();

    // read increase/decrease
    c = getchar() - '0';
    if (c != DECREASE && c != INCREASE) {
        printf("ERROR: increase/decrease must be %d or %d\n", DECREASE, INCREASE);
        while(getchar() != '\n');
        return 1;
    }
    *increase_decrease = c;

    // discard remaining input stream characters
    while(getchar() != '\n');

    printf("User inputted: %s %d %d\n", input_filename, *scaling_factor, *increase_decrease);
    return 0;
}

/*
	------------------------------------------------------------------------------------------------
	parses user input

	parse user inputted: input filename, scaling factor and increase/decrease
	------------------------------------------------------------------------------------------------
*/
alt_u32 partOfImageUserInput(ImagePartParameters_t* image_part_parameters) {
    // user input parsing
    alt_32 c;

    printf("{whole/part} {NC/row} {NC/col} {NC/width} {NC/height}\n");
    printf("     %u/%u                                                                                   \n", WHOLE, PART);

    // read whole/part
    c = getchar() - '0';
    if (c != WHOLE && c != PART) {
        printf("ERROR: whole/part must be %d or %d\n", WHOLE, PART);
        while(getchar() != '\n');
        return 1;
    }
    image_part_parameters->whole_part = c;

    if (image_part_parameters->whole_part == WHOLE) {
    	while(getchar() != '\n');
    	return 0;
    }

    // skip space
    getchar();

    // read starting_point row
    image_part_parameters->row = 0;
    while(isdigit(c = getchar())) {
    	image_part_parameters->row = image_part_parameters->row * 10 + (c - '0');
    }

    // read starting_point col
    image_part_parameters->col = 0;
    while(isdigit(c = getchar())) {
    	image_part_parameters->col = image_part_parameters->col * 10 + (c - '0');
    }

    // read starting_point col
    image_part_parameters->width = 0;
    while(isdigit(c = getchar())) {
    	image_part_parameters->width = image_part_parameters->width * 10 + (c - '0');
    }

    // read starting_point col
    image_part_parameters->height = 0;
    while(isdigit(c = getchar())) {
    	image_part_parameters->height = image_part_parameters->height * 10 + (c - '0');
    }

    // discard remaining input stream characters
    while(c != '\n') {
    	c = getchar();
    }

    printf("User inputted: %u %u %u %u %u\n", (unsigned int)image_part_parameters->whole_part, 
											  (unsigned int)image_part_parameters->row, 
											  (unsigned int)image_part_parameters->col, 
											  (unsigned int)image_part_parameters->width, 
											  (unsigned int)image_part_parameters->height);
    return 0;
}

/*
	------------------------------------------------------------------------------------------------
	creates buffer for input image in dynamic memory and reads pixel values from bin input file

	read input image width, height and pixels from binary file
	------------------------------------------------------------------------------------------------
*/
alt_u32 loadImage(alt_8 *input_filename, Image_t *input_image) {
    FILE *ptr_input_file;
	
	// nios compatible filename
    alt_8 input_filename_nios[INPUT_FILENAME_MAX_LEN+10] = {'/','m','n','t','/','h','o','s','t','/','i','n','p','u','t','/','\0'};
    strcat((char*)input_filename_nios, (char*)input_filename);
#if VERBOSE_LEVEL>0
    printf("Input filename with ext: %s\n", input_filename_nios);
#endif

    // open input file
    ptr_input_file = fopen((char *)input_filename_nios, "rb");
    if (ptr_input_file == NULL)
    {
        printf("ERROR: Unable to open file \"%s\"!\n", input_filename);
        return 1;
    }

    // read image width and height
    fread(&(input_image->width),sizeof(input_image->width),1,ptr_input_file);
	fread(&(input_image->height),sizeof(input_image->height),1,ptr_input_file);
#if VERBOSE_LEVEL>0
    printf("input_image_width = %u\n", (unsigned int)input_image->width);
	printf("input_image_height = %u\n", (unsigned int)input_image->height);
#endif

    // allocate buffer for input image
	input_image->pixels = (alt_u8**)malloc(input_image->height * sizeof(alt_u8*));
	if (input_image->pixels == NULL) {
        printf("ERROR: Unable to allocate buffer for input image.\n");
        return 1;
    }
	for (alt_u32 i = 0; i < input_image->height; i++) {
		(input_image->pixels)[i] = (alt_u8*)malloc(input_image->width * sizeof(alt_u8));
		if ((input_image->pixels)[i] == NULL) {
			printf("ERROR: Unable to allocate buffer for input image.\n");
			for(alt_u32 j = 0; j < i; j++) {
				free((input_image->pixels)[j]);
			}
			free(input_image->pixels);
			return 1;
		}
    }

    // read all the pixels
#if VERBOSE_LEVEL>0
	printf("Start of reading all the pixels from file into input image buffer.\n");
#endif
	for (alt_u32 i = 0; i < input_image->height; i++) {
		fread((input_image->pixels)[i],input_image->width * sizeof(alt_u8),1,ptr_input_file);
    }
#if VERBOSE_LEVEL>0
	printf("End of reading all the pixels from file into input image buffer.\n");
#endif

    // close input file
    fclose(ptr_input_file);

#if VERBOSE_LEVEL>0
	printf("loadImage end.\n");
#endif
    return 0;
}

/*
	------------------------------------------------------------------------------------------------
	form input image based on part of image input instructions
	------------------------------------------------------------------------------------------------
*/
alt_u32 formInputImage(ImagePartParameters_t image_part_parameters, Image_t *image) {

	alt_u8 **temp;

	// if whole image needs to be processed nothing needs to change => just exit
	if (image_part_parameters.whole_part == WHOLE) {
		return 0;
	}

	if (image_part_parameters.row + image_part_parameters.height > image->height) {
		printf("ERROR: Part of image rows exceed input image\n");
		return 1;
	}

	if (image_part_parameters.col + image_part_parameters.width > image->width) {
		printf("ERROR: Part of image columns exceed input image\n");
		return 1;
	}

    // allocate buffer for new input image
	temp = (alt_u8**)malloc(image_part_parameters.height * sizeof(alt_u8*));
	if (temp == NULL) {
        printf("ERROR: Unable to allocate buffer for new input image.\n");
        return 1;
    }
	for (alt_u32 i = 0; i < image_part_parameters.height; i++) {
		temp[i] = (alt_u8*)malloc(image_part_parameters.width * sizeof(alt_u8));
		if (temp[i] == NULL) {
			printf("ERROR: Unable to allocate buffer for new input image.\n");
			for(alt_u32 j = 0; j < i; j++) {
				free(temp[j]);
			}
			free(temp);
			return 1;
		}
    }

	for (alt_u32 i = 0; i < image_part_parameters.height; i++) {
		for (alt_u32 j = 0; j < image_part_parameters.width; j++) {
			temp[i][j] = (image->pixels)[image_part_parameters.row + i][image_part_parameters.col + j];
		}
	}

    // free dynamic memory
	for(alt_u32 i = 0; i < image->height; i++) {
		free((image->pixels)[i]);
	}
	free(image->pixels);

	image->height = image_part_parameters.height;
	image->width = image_part_parameters.width;
	image->pixels = temp;
	
#if VERBOSE_LEVEL>0
    printf("input_image_height = %u\n", (unsigned int)image->height);
    printf("input_image_width = %u\n",  (unsigned int)image->width);
    printf("formInputImage end\n");
#endif
    return 0;
}

/*
	------------------------------------------------------------------------------------------------
	creates buffer for output image in dynamic memory

	form output image buffer and parameters
	------------------------------------------------------------------------------------------------
*/
alt_u32 formOutputImage(
		ScalingFactor_t scaling_factor,
		IncreaseDecreaseResolution_t increase_decrease,
		Image_t input_image,
		Image_t *output_image) {

    // form output image width and height
    if (increase_decrease == INCREASE) {
		// checking potential overflow that may occur as a result of multiplication
		if ((BIGGEST_32BIT_UNSIGNED_NUMBER / scaling_factor) < input_image.height) {
			printf("ERROR: Output image height can not be stored in unsigned 32bit variable.\n");
			return 1;
		}
        output_image->height = input_image.height * scaling_factor;

		// checking potential overflow that may occur as a result of multiplication
		if ((BIGGEST_32BIT_UNSIGNED_NUMBER / scaling_factor) < input_image.width) {
			printf("ERROR: Output image width can not be stored in unsigned 32bit variable.\n");
			return 1;
		}
        output_image->width = input_image.width * scaling_factor;
    } else {
		if (input_image.height % scaling_factor) {
			// mod != 0
			output_image->height = input_image.height / scaling_factor + 1;
			output_image->width  = input_image.width  / scaling_factor + 1;
		} else {
			// mod = 0
			output_image->height = input_image.height / scaling_factor;
			output_image->width  = input_image.width  / scaling_factor;
		}
    }

    // allocate buffer for output image
	output_image->pixels = (alt_u8**)malloc(output_image->height * sizeof(alt_u8*));
	if (output_image->pixels == NULL) {
        printf("ERROR: Unable to allocate buffer for output image.\n");
        return 1;
    }
	for (alt_u32 i = 0; i < output_image->height; i++) {
		(output_image->pixels)[i] = (alt_u8*)malloc(output_image->width * sizeof(alt_u8));
		if ((output_image->pixels)[i] == NULL) {
			printf("ERROR: Unable to allocate buffer for output image.\n");
			for(alt_u32 j = 0; j < i; j++) {
				free((output_image->pixels)[j]);
			}
			free(output_image->pixels);
			return 1;
		}
    }

#if VERBOSE_LEVEL>0
    printf("output_image_height = %u\n", (unsigned int)output_image->height);
    printf("output_image_width = %u\n", (unsigned int)output_image->width);
    printf("formOutputImage end\n");
#endif
    return 0;
}

/*
	------------------------------------------------------------------------------------------------
	does acc_scale to image utilising NIOS processor

	process: input image ---> output image
	------------------------------------------------------------------------------------------------
*/
alt_u32 swProcessImage(
        ScalingFactor_t scaling_factor,
        IncreaseDecreaseResolution_t increase_decrease,
        Image_t input_image,
        Image_t output_image) {

    if (increase_decrease == INCREASE) {
        alt_u32 col_mul_cnt = 0;
        alt_u32 row_mul_cnt = 0;
        alt_u32 in_row = 0;
        alt_u32 in_col = 0;
        for(alt_u32 out_row = 0; out_row < output_image.height; out_row++) {
			for(alt_u32 out_col = 0; out_col < output_image.width; out_col++) {
				(output_image.pixels)[out_row][out_col] = (input_image.pixels)[in_row][in_col];

				col_mul_cnt++;

				if (col_mul_cnt == scaling_factor) {
					col_mul_cnt = 0;
					in_col++;

					if (in_col == input_image.width) {
						in_col = 0;
						row_mul_cnt++;

						if (row_mul_cnt == scaling_factor) {
							row_mul_cnt = 0;
							in_row++;
						}
					}
				}
			}
        }
    } else {
        alt_u32 in_row = 0;
        alt_u32 in_col = 0;
		for(alt_u32 out_row = 0; out_row < output_image.height; out_row++) {
			for(alt_u32 out_col = 0; out_col < output_image.width; out_col++) {
				(output_image.pixels)[out_row][out_col] = (input_image.pixels)[in_row][in_col];

				if (in_col >= input_image.width - scaling_factor) {
					in_row += scaling_factor;
					in_col = 0;
				} else {
					in_col += scaling_factor;
				}
			}
		}
    }

#if VERBOSE_LEVEL>0
    printf("swProcessImage end.\n");
#endif
    return 0;
}

/*
	------------------------------------------------------------------------------------------------
	stores output image pixel values in bin output file

	write output image width, height and pixels to binary file
	------------------------------------------------------------------------------------------------
*/
alt_u32 storeImage(alt_8 *filename, Image_t image) {
    FILE *ptr_output_file;

    // open output file
    ptr_output_file = fopen((char*)filename,"wb");
    if (ptr_output_file == NULL)
    {
        printf("Unable to open file \"%s\"!\n", filename);
        return 1;
    }

    // write image width and height
    fwrite(&(image.width),sizeof(image.width),1,ptr_output_file);
	fwrite(&(image.height),sizeof(image.height),1,ptr_output_file);

    // write all the pixels
	for (alt_u32 i = 0; i < image.height; i++) {
		fwrite((image.pixels)[i],image.width * sizeof(alt_u8),1,ptr_output_file);
    }

    // close output file
    fclose(ptr_output_file);

#if VERBOSE_LEVEL>0
    printf("storeImage end.\n");
#endif
    return 0;
}


/*
	------------------------------------------------------------------------------------------------
	Allocating descriptor table space from main memory.
	------------------------------------------------------------------------------------------------
*/
alt_u32 createDescriptors(
		alt_sgdma_descriptor ** transmit_descriptors_p,
        alt_sgdma_descriptor ** transmit_descriptors_copy_p,
        alt_sgdma_descriptor ** receive_descriptors_p,
        alt_sgdma_descriptor ** receive_descriptors_copy_p,
		Image_t input_image,
		Image_t output_image)
{
	/* Allocate some big buffers to hold all descriptors which will slide until
	 * the first 32 byte boundary is found */
	void * temp_ptr;
	alt_u32 input_desctiptors_count;
	alt_u32 output_desctiptors_count;
	alt_u32 input_desctiptors_count_row;
	alt_u32 output_desctiptors_count_row;
	alt_sgdma_descriptor *transmit_descriptors, *receive_descriptors;
	alt_u32 current_descriptor;

	/*
	   * Allocation of the transmit descriptors                   *
	   * - First allocate a large buffer to the temporary pointer *
	   * - Second check for successful memory allocation          *
	   * - Third put this memory location into the pointer copy   *
	   *   to be freed before the program exits                   *
	   * - Forth slide the temporary pointer until it lies on a 32*
	   *   byte boundary (descriptor master is 256 bits wide)     */

	// calculate number of descriptors for input image
	// number of rows * number of descriptors per row
	if (input_image.width % DESCRIPTOR_BUFFER_LEN_MAX) {
		// checking potential overflow that may occur as a result of multiplication
		if ((BIGGEST_32BIT_UNSIGNED_NUMBER / (input_image.width / DESCRIPTOR_BUFFER_LEN_MAX + 1)) < input_image.height) {
			printf("ERROR: While allocating descriptors. Input image is too big.\n");
			return 1;
		}
		input_desctiptors_count_row = input_image.width / DESCRIPTOR_BUFFER_LEN_MAX + 1;
		input_desctiptors_count = input_image.height * input_desctiptors_count_row;
	} else {
		// checking potential overflow that may occur as a result of multiplication
		if ((BIGGEST_32BIT_UNSIGNED_NUMBER / (input_image.width / DESCRIPTOR_BUFFER_LEN_MAX)) < input_image.height) {
			printf("ERROR: While allocating descriptors. Input image is too big.\n");
			return 1;
		}
		input_desctiptors_count_row = input_image.width / DESCRIPTOR_BUFFER_LEN_MAX;
		input_desctiptors_count = input_image.height * input_desctiptors_count_row;
	}

#if VERBOSE_LEVEL>0
    printf("Number of input descriptors: %u\n", (unsigned int)input_desctiptors_count);
#endif

	// checking potential overflow that may occur as a result of addition
	if ((BIGGEST_32BIT_UNSIGNED_NUMBER - 2) < input_desctiptors_count) {
		printf("ERROR: While allocating descriptors. Input image is too big.\n");
		return 1;
	}

	// checking potential overflow that may occur as a result of multiplication
	if ((BIGGEST_32BIT_UNSIGNED_NUMBER / ALTERA_AVALON_SGDMA_DESCRIPTOR_SIZE) < input_desctiptors_count + 2) {
		printf("ERROR: While allocating descriptors. Input image is too big.\n");
		return 1;
	}

	temp_ptr = malloc((input_desctiptors_count + 2) * ALTERA_AVALON_SGDMA_DESCRIPTOR_SIZE);
	if(temp_ptr == NULL)
	{
		printf("ERROR: Failed to allocate memory for the transmit descriptors\n");
		return 1;
	}
	*transmit_descriptors_copy_p = (alt_sgdma_descriptor *)temp_ptr;

	while((((alt_u32)temp_ptr) % ALTERA_AVALON_SGDMA_DESCRIPTOR_SIZE) != 0)
	{
		temp_ptr++;  // slide the pointer until 32 byte boundary is found
	}

	transmit_descriptors = (alt_sgdma_descriptor *)temp_ptr;

	*transmit_descriptors_p = transmit_descriptors;

	/* Clear out the null descriptor owned by hardware bit.  These locations
	 * came from the heap so we don't know what state the bytes are in (owned bit could be high).*/
	transmit_descriptors[input_desctiptors_count].control = 0;

	/*
	   * Allocation of the receive descriptors                    *
	   * - First allocate a large buffer to the temporary pointer *
	   * - Second check for successful memory allocation          *
	   * - Third put this memory location into the pointer copy   *
	   *   to be freed before the program exits                   *
	   * - Forth slide the temporary pointer until it lies on a 32*
	   *   byte boundary (descriptor master is 256 bits wide)     */

	// calculate number of descriptors for input image
	// number of rows * number of descriptors per row
	if (output_image.width % DESCRIPTOR_BUFFER_LEN_MAX) {
		// checking potential overflow that may occur as a result of multiplication
		if ((BIGGEST_32BIT_UNSIGNED_NUMBER / (output_image.width / DESCRIPTOR_BUFFER_LEN_MAX + 1)) < output_image.height) {
			printf("ERROR: While allocating descriptors. Input image is too big.\n");
			return 1;
		}
		output_desctiptors_count_row = output_image.width / DESCRIPTOR_BUFFER_LEN_MAX + 1;
		output_desctiptors_count = output_image.height * output_desctiptors_count_row;
	} else {
		// checking potential overflow that may occur as a result of multiplication
		if ((BIGGEST_32BIT_UNSIGNED_NUMBER / (output_image.width / DESCRIPTOR_BUFFER_LEN_MAX)) < output_image.height) {
			printf("ERROR: While allocating descriptors. Input image is too big.\n");
			return 1;
		}
		output_desctiptors_count_row = output_image.width / DESCRIPTOR_BUFFER_LEN_MAX;
		output_desctiptors_count = output_image.height * output_desctiptors_count_row;
	}

#if VERBOSE_LEVEL>0
    printf("Number of output descriptors: %u\n", (unsigned int)output_desctiptors_count);
#endif

	// checking potential overflow that may occur as a result of addition
	if ((BIGGEST_32BIT_UNSIGNED_NUMBER - 2) < output_desctiptors_count) {
		printf("ERROR: While allocating descriptors. Input image is too big.\n");
		return 1;
	}

	// checking potential overflow that may occur as a result of multiplication
	if ((BIGGEST_32BIT_UNSIGNED_NUMBER / ALTERA_AVALON_SGDMA_DESCRIPTOR_SIZE) < output_desctiptors_count + 2) {
		printf("ERROR: While allocating descriptors. Input image is too big.\n");
		return 1;
	}

	temp_ptr = malloc((output_desctiptors_count + 2) * ALTERA_AVALON_SGDMA_DESCRIPTOR_SIZE);
	if(temp_ptr == NULL)
	{
		printf("ERROR: Failed to allocate memory for the transmit descriptors\n");
		return 1;
	}
	*receive_descriptors_copy_p = (alt_sgdma_descriptor *)temp_ptr;

	while((((alt_u32)temp_ptr) % ALTERA_AVALON_SGDMA_DESCRIPTOR_SIZE) != 0)
	{
		temp_ptr++;  // slide the pointer until 32 byte boundary is found
	}

	receive_descriptors = (alt_sgdma_descriptor *)temp_ptr;
	*receive_descriptors_p = receive_descriptors;

	/* Clear out the null descriptor owned by hardware bit.  These locations
	 * came from the heap so we don't know what state the bytes are in (owned bit could be high).*/
	receive_descriptors[output_desctiptors_count].control = 0;

	// fill allocated memory with transmit descriptor data
	current_descriptor = 0;
	for (alt_u32 i = 0; i < input_image.height; i++) {
		for (alt_u32 j = 0; j < input_desctiptors_count_row; j++) {
			// current descriptor
			// number of bytes to send
			alt_u32 buffer_length;
			if (j < input_desctiptors_count_row-1) {
				// not last buffer in this row
				buffer_length = DESCRIPTOR_BUFFER_LEN_MAX;
			} else {
				// last buffer in this row
				buffer_length = input_image.width-j*DESCRIPTOR_BUFFER_LEN_MAX;
			}

			/* This will create a descriptor that is capable of transmitting data from an Avalon-MM buffer
			 * to a packet enabled Avalon-ST FIFO component */
			alt_avalon_sgdma_construct_mem_to_stream_desc(
					&transmit_descriptors[current_descriptor],  							// current descriptor pointer
					&transmit_descriptors[current_descriptor+1], 							// next descriptor pointer
					(alt_u32*)&(input_image.pixels)[i][j*DESCRIPTOR_BUFFER_LEN_MAX],	// read buffer location
					(alt_u16)buffer_length,  								// length of the buffer
					0, 		// reads are not from a fixed location
					0,		// start of packet is disabled for the Avalon-ST interfaces
					0, 		// end of packet is disabled for the Avalon-ST interfaces,
					0);  	// there is only one channel
			current_descriptor++;
		}
	}

	// fill allocated memory with receive descriptor data
	current_descriptor = 0;
	for (alt_u32 i = 0; i < output_image.height; i++) {
		for (alt_u32 j = 0; j < output_desctiptors_count_row; j++) {
			// number of bytes to send
			alt_u32 buffer_length;
			if (j < output_desctiptors_count_row-1) {
				// not last buffer in this row
				buffer_length = DESCRIPTOR_BUFFER_LEN_MAX;
			} else {
				// last buffer in this row
				buffer_length = output_image.width-j*DESCRIPTOR_BUFFER_LEN_MAX;
			}

			/* This will create a descriptor that is capable of transmitting data from an Avalon-MM buffer
			 * to a packet enabled Avalon-ST FIFO component */
			alt_avalon_sgdma_construct_stream_to_mem_desc(
					&receive_descriptors[current_descriptor],  							// current descriptor pointer
					&receive_descriptors[current_descriptor+1], 							// next descriptor pointer
					(alt_u32*)&(output_image.pixels)[i][j*DESCRIPTOR_BUFFER_LEN_MAX],	// write buffer location
					(alt_u16)buffer_length,  								// length of the buffer
					0); // writes are not to a fixed location
			current_descriptor++;
		}
	}

#if VERBOSE_LEVEL>0
    printf("createDescriptors end\n");
#endif
	return 0;  // no failures in allocation
}


/*
	------------------------------------------------------------------------------------------------
	does acc_scale to image utilising hw accelerator

	process: input image ---> output image
	------------------------------------------------------------------------------------------------
*/
alt_u32 hwProcessImage(
		alt_sgdma_dev * transmit_DMA,
		alt_sgdma_descriptor * transmit_descriptors,
		volatile alt_u16 * tx_done_p,
		alt_sgdma_dev * receive_DMA,
		alt_sgdma_descriptor * receive_descriptors,
		volatile alt_u16 * rx_done_p,
		ScalingFactor_t scaling_factor,
		IncreaseDecreaseResolution_t increase_decrease,
		Image_t input_image) {

	// Configure acc_scale module.
#if VERBOSE_LEVEL>0
	printf("width0: %u\n", (unsigned int)((input_image.width >> 0 ) & 0x000000FF));
	printf("width1: %u\n", (unsigned int)((input_image.width >> 8 ) & 0x000000FF));
	printf("width2: %u\n", (unsigned int)((input_image.width >> 16 ) & 0x000000FF));
	printf("width3: %u\n", (unsigned int)((input_image.width >> 24 ) & 0x000000FF));

	printf("height0: %u\n", (unsigned int)((input_image.height >> 0 ) & 0x000000FF));
	printf("height1: %u\n", (unsigned int)((input_image.height >> 8 ) & 0x000000FF));
	printf("height2: %u\n", (unsigned int)((input_image.height >> 16 ) & 0x000000FF));
	printf("height3: %u\n", (unsigned int)((input_image.height >> 24 ) & 0x000000FF));
#endif
	// width
	IOWR_8DIRECT(ACC_SCALE_BASE, ADDR_WIDTH_0, (alt_8)((input_image.width >> 0 ) & 0x000000FF));
	IOWR_8DIRECT(ACC_SCALE_BASE, ADDR_WIDTH_1, (alt_8)((input_image.width >> 8 ) & 0x000000FF));
	IOWR_8DIRECT(ACC_SCALE_BASE, ADDR_WIDTH_2, (alt_8)((input_image.width >> 16) & 0x000000FF));
	IOWR_8DIRECT(ACC_SCALE_BASE, ADDR_WIDTH_3, (alt_8)((input_image.width >> 24) & 0x000000FF));

	// height
	IOWR_8DIRECT(ACC_SCALE_BASE, ADDR_HEIGHT_0, (alt_8)((input_image.height >> 0 ) & 0x000000FF));
	IOWR_8DIRECT(ACC_SCALE_BASE, ADDR_HEIGHT_1, (alt_8)((input_image.height >> 8 ) & 0x000000FF));
	IOWR_8DIRECT(ACC_SCALE_BASE, ADDR_HEIGHT_2, (alt_8)((input_image.height >> 16) & 0x000000FF));
	IOWR_8DIRECT(ACC_SCALE_BASE, ADDR_HEIGHT_3, (alt_8)((input_image.height >> 24) & 0x000000FF));

	// status is read only

	// control
	if (increase_decrease == INCREASE) {
#if VERBOSE_LEVEL>0
		printf("control: %02x\n", (unsigned int)(BIT_CONTROL_START + BIT_CONTROL_INCREASE + scaling_factor));
#endif
		IOWR_8DIRECT(ACC_SCALE_BASE, ADDR_CONTROL, (unsigned char)(BIT_CONTROL_START + BIT_CONTROL_INCREASE + scaling_factor));
	} else {
#if VERBOSE_LEVEL>0
		printf("control: %02x\n", (unsigned int)(BIT_CONTROL_START + scaling_factor));
#endif
		IOWR_8DIRECT(ACC_SCALE_BASE, ADDR_CONTROL, (unsigned char)(BIT_CONTROL_START + scaling_factor));
	}

	// Starting both the transmit and receive transfers

#if VERBOSE_LEVEL>0
    printf("Starting up the SGDMA engines\n");
#endif
	// Start non blocking transfer with DMA modules.
	if(alt_avalon_sgdma_do_async_transfer(transmit_DMA, &transmit_descriptors[0]) != 0) {
		printf("Writing the head of the transmit descriptor list to the DMA failed\n");
		return 1;
	}
	if(alt_avalon_sgdma_do_async_transfer(receive_DMA, &receive_descriptors[0]) != 0) {
		printf("Writing the head of the receive descriptor list to the DMA failed\n");
		return 1;
	}

	// Blocking until the SGDMA interrupts fire
	while(*tx_done_p < 1) {}
#if VERBOSE_LEVEL>0
	printf("The transmit SGDMA has completed\n");
#endif
	while(*rx_done_p < 1) {}
#if VERBOSE_LEVEL>0
	printf("The receive SGDMA has completed\n");
#endif

	*tx_done_p = 0;
	*rx_done_p = 0;

	// Stop the SGDMAs
	alt_avalon_sgdma_stop(transmit_DMA);
	alt_avalon_sgdma_stop(receive_DMA);

#if VERBOSE_LEVEL>0
	printf("hwProcessImage end\n");
#endif
	return 0;
}

/*
	------------------------------------------------------------------------------------------------
	validate hw results that are in output image
	
	same as swProcessImage, instead of writing data to output image just compare data
	------------------------------------------------------------------------------------------------
*/
alt_u32 validateResultsHW(
        ScalingFactor_t scaling_factor,
        IncreaseDecreaseResolution_t increase_decrease,
        Image_t input_image,
        Image_t output_image) {

    if (increase_decrease == INCREASE) {
        alt_u32 col_mul_cnt = 0;
        alt_u32 row_mul_cnt = 0;
        alt_u32 in_row = 0;
        alt_u32 in_col = 0;
        for(alt_u32 out_row = 0; out_row < output_image.height; out_row++) {
			for(alt_u32 out_col = 0; out_col < output_image.width; out_col++) {
				if ( (output_image.pixels)[out_row][out_col] != (input_image.pixels)[in_row][in_col] ) {
					// mismatch => hw results are not good
					printf("ValidateResultsHW: FAIL at pixel [%u,%u]\n", (unsigned int)out_row, (unsigned int)out_col);
					return 0;
				}

				col_mul_cnt++;

				if (col_mul_cnt == scaling_factor) {
					col_mul_cnt = 0;
					in_col++;

					if (in_col == input_image.width) {
						in_col = 0;
						row_mul_cnt++;

						if (row_mul_cnt == scaling_factor) {
							row_mul_cnt = 0;
							in_row++;
						}
					}
				}
			}
        }
    } else {
        alt_u32 in_row = 0;
        alt_u32 in_col = 0;
		for(alt_u32 out_row = 0; out_row < output_image.height; out_row++) {
			for(alt_u32 out_col = 0; out_col < output_image.width; out_col++) {
				if ( (output_image.pixels)[out_row][out_col] != (input_image.pixels)[in_row][in_col] ) {
					// mismatch => hw results are not good
					printf("ValidateResultsHW: FAIL at pixel [%u,%u]\n", (unsigned int)out_row, (unsigned int)out_col);
					return 0;
				}

				if (in_col >= input_image.width - scaling_factor) {
					in_row += scaling_factor;
					in_col = 0;
				} else {
					in_col += scaling_factor;
				}
			}
		}
    }

	printf("ValidateResultsHW: SUCCESS!\n");
    return 0;
}

/*
	------------------------------------------------------------------------------------------------
	main
	------------------------------------------------------------------------------------------------
*/

// s2m sgdma interrupt
void transmit_callback_function(void * context)
{
	alt_u16 *tx_done = (alt_u16*) context;
	(*tx_done)++;  /* main will be polling for this value being 1 */
}

void receive_callback_function(void * context)
{
	alt_u16 *rx_done = (alt_u16*) context;
	(*rx_done)++;  /* main will be polling for this value being 1 */
}

int main () {
	/* Since descriptors need to be placed in memory locations aligned to
	 * descriptor size (which is 4 bytes), larger chunk of memory is first allocated
	 * and then aligned location is found. Descriptor pointers point to descriptors
	 * placed on aligned location and copy pointer points to entire allocated memory.
	 * Copy pointers are needed for properly freeing of allocated memory. */
	alt_sgdma_descriptor *m2s_desc, *m2s_desc_copy;
	alt_sgdma_descriptor *s2m_desc, *s2m_desc_copy;

	// Open a SG-DMA for MM-->ST and ST-->MM (two SG-DMAs are present)
	alt_sgdma_dev * sgdma_m2s = alt_avalon_sgdma_open("/dev/sgdma_m2s");
	alt_sgdma_dev * sgdma_s2m = alt_avalon_sgdma_open("/dev/sgdma_s2m");

	// Values used by hwProcessImage - sgdma_m2s, sgdma_s2m - to signal end of job
	volatile alt_u16 tx_done = 0;
	volatile alt_u16 rx_done = 0;

	alt_8 input_filename[INPUT_FILENAME_MAX_LEN];
	alt_8 output_filename[OUTPUT_FILENAME_MAX_LEN];
	ScalingFactor_t scaling_factor;
	IncreaseDecreaseResolution_t increase_decrease;

	ImagePartParameters_t image_part_parameters;

	Image_t input_image;
	Image_t output_image;

    alt_32 choice;
    while(1) {
        printf("Another processing: {1}\n");
        printf("Exit:               {0}\n");
        choice = getchar();
        while(getchar() != '\n');

        switch(choice) {
        case '1':

			// Make sure SG-DMAs were opened correctly
			if(sgdma_m2s == NULL)
			{
				printf("Could not open the transmit SG-DMA\n");
				break;
			}
			if(sgdma_s2m == NULL)
			{
				printf("Could not open the receive SG-DMA\n");
				break;
			}

            // ----------------------------------------------------------------
            // parse user inputted: input filename, scaling factor and increase/decrease
			// ----------------------------------------------------------------
            if (initialUserInput(input_filename, &scaling_factor, &increase_decrease)) {
                break;
            }

			// ----------------------------------------------------------------
            // parse user inputted: process whole image or only part of it
            // ----------------------------------------------------------------
            if (partOfImageUserInput(&image_part_parameters)) {
                break;
            }

            // ----------------------------------------------------------------
            // read input image height, width and pixels from binary file
			// ----------------------------------------------------------------
            if (loadImage(input_filename, &input_image)) {
                break;
            }

            // ----------------------------------------------------------------
            // form input image based on part of image input instructions
			// ----------------------------------------------------------------
            if(formInputImage(image_part_parameters, &input_image)) {
                // free dynamic memory
				for(alt_u32 i = 0; i < input_image.height; i++) {
					free((input_image.pixels)[i]);
				}
				free(input_image.pixels);
                break;
            }

            // ----------------------------------------------------------------
            // form output image buffer and parameters
			// ----------------------------------------------------------------
            if (formOutputImage(scaling_factor, increase_decrease, input_image, &output_image)) {
                // free dynamic memory
				for(alt_u32 i = 0; i < input_image.height; i++) {
					free((input_image.pixels)[i]);
				}
				free(input_image.pixels);
                break;
            }

			// ----------------------------------------------------------------
			// Allocating descriptor table space from main memory.
			// ----------------------------------------------------------------
			if (createDescriptors(
					&m2s_desc,
                    &m2s_desc_copy,
					&s2m_desc,
                    &s2m_desc_copy,
                    input_image,
                    output_image)) {
				printf("Allocating the descriptor memory failed...\n");
				// free dynamic memory
				for(alt_u32 i = 0; i < input_image.height; i++) {
					free((input_image.pixels)[i]);
				}
				free(input_image.pixels);
				for(alt_u32 i = 0; i < output_image.height; i++) {
					free((output_image.pixels)[i]);
				}
				free(output_image.pixels);
                break;
			}

			/*
			 * Register the ISRs that will get called when each (full)
			 * transfer completes. When park bit is set, processed
			 * descriptors are not invalidated (OWNED_BY_HW bit stays 1)
			 * meaning that the same descriptors can be used for new
			 * transfers.
			 */
			alt_avalon_sgdma_register_callback(
					sgdma_m2s,
					&transmit_callback_function,
					(ALTERA_AVALON_SGDMA_CONTROL_IE_GLOBAL_MSK |
					 ALTERA_AVALON_SGDMA_CONTROL_IE_CHAIN_COMPLETED_MSK |
					 ALTERA_AVALON_SGDMA_CONTROL_PARK_MSK),
					(void*)&rx_done);
			alt_avalon_sgdma_register_callback(
					sgdma_s2m,
					&receive_callback_function,
					(ALTERA_AVALON_SGDMA_CONTROL_IE_GLOBAL_MSK |
					 ALTERA_AVALON_SGDMA_CONTROL_IE_CHAIN_COMPLETED_MSK |
					 ALTERA_AVALON_SGDMA_CONTROL_PARK_MSK),
				   (void*)&tx_done);

			/*
			 * Reset performance counter. Start global counter.
			 * Enable section counters.
			 */

			PERF_RESET(PERFORMANCE_COUNTER_BASE);
			PERF_START_MEASURING(PERFORMANCE_COUNTER_BASE);

			/*
			 * Data processing with 2 different functions - software and hardware.
			 * Performance is measured for each approach.
			 */
			alt_dcache_flush_all();

			PERF_BEGIN(PERFORMANCE_COUNTER_BASE, 1);

            // ----------------------------------------------------------------
            // SW process: input image ---> output image
			// ----------------------------------------------------------------
            if (swProcessImage(
                    scaling_factor,
                    increase_decrease,
                    input_image,
                    output_image)) {
				printf("Scale function software processing failed...\n");
                // free dynamic memory
				for(alt_u32 i = 0; i < input_image.height; i++) {
					free((input_image.pixels)[i]);
				}
				free(input_image.pixels);
				for(alt_u32 i = 0; i < output_image.height; i++) {
					free((output_image.pixels)[i]);
				}
				free(output_image.pixels);
				// v2
                free(m2s_desc_copy);
                free(s2m_desc_copy);
                break;
            }

		    PERF_END(PERFORMANCE_COUNTER_BASE, 1);

#if WRITE_OUTPUTS_TO_FILE>0
            // ----------------------------------------------------------------
            // write output image height, width and pixels to binary file
			// ----------------------------------------------------------------
			sprintf((char*)output_filename, "%s_%s%u.bin", OUTPUT_FILENAME_SW, ((increase_decrease == INCREASE) ? "inc" : "dec"), scaling_factor);
			printf("Output filename software processing: %s\n", output_filename);
            if (storeImage(output_filename, output_image)) {
                // free dynamic memory
				for(alt_u32 i = 0; i < input_image.height; i++) {
					free((input_image.pixels)[i]);
				}
				free(input_image.pixels);
				for(alt_u32 i = 0; i < output_image.height; i++) {
					free((output_image.pixels)[i]);
				}
				free(output_image.pixels);
				// v2
                free(m2s_desc_copy);
                free(s2m_desc_copy);
                break;
            }
#endif

            // ----------------------------------------------------------------
            // reset output image data to all 0
			// ----------------------------------------------------------------
            for(alt_u32 i = 0; i < output_image.height; i++) {
            	for(alt_u32 j = 0; j < output_image.width; j++) {
            		(output_image.pixels)[i][j] = 0;
            	}
            }
			
            PERF_BEGIN(PERFORMANCE_COUNTER_BASE, 2);

            // ----------------------------------------------------------------
            // HW process: input image ---> output image
			// ----------------------------------------------------------------
			if (hwProcessImage(
					sgdma_m2s,
					m2s_desc,
					&tx_done,
					sgdma_s2m,
					s2m_desc,
					&rx_done,
                    scaling_factor,
                    increase_decrease,
                    input_image)) {
				printf("Scale function hardware processing failed...\n");
                // free dynamic memory
				for(alt_u32 i = 0; i < input_image.height; i++) {
					free((input_image.pixels)[i]);
				}
				free(input_image.pixels);
				for(alt_u32 i = 0; i < output_image.height; i++) {
					free((output_image.pixels)[i]);
				}
				free(output_image.pixels);
				// v2
                free(m2s_desc_copy);
                free(s2m_desc_copy);
                break;
            }

            PERF_END(PERFORMANCE_COUNTER_BASE, 2);

#if WRITE_OUTPUTS_TO_FILE>0
            // ----------------------------------------------------------------
            // write output image height, width and pixels to binary file
			// ----------------------------------------------------------------
			sprintf((char*)output_filename, "%s_%s%u.bin", OUTPUT_FILENAME_HW, ((increase_decrease == INCREASE) ? "inc" : "dec"), scaling_factor);
		    printf("Output filename hardware processing: %s\n", output_filename);
            if (storeImage(output_filename, output_image)) {
                // free dynamic memory
				for(alt_u32 i = 0; i < input_image.height; i++) {
					free((input_image.pixels)[i]);
				}
				free(input_image.pixels);
				for(alt_u32 i = 0; i < output_image.height; i++) {
					free((output_image.pixels)[i]);
				}
				free(output_image.pixels);
				// v2
                free(m2s_desc_copy);
                free(s2m_desc_copy);
                break;
            }
#endif

            // ----------------------------------------------------------------
			// validate hwProcessImage results
            // ----------------------------------------------------------------
            if (validateResultsHW(
                    scaling_factor,
                    increase_decrease,
                    input_image,
                    output_image)) {
				printf("Validate Results HW function failed...\n");
                // free dynamic memory
				for(alt_u32 i = 0; i < input_image.height; i++) {
					free((input_image.pixels)[i]);
				}
				free(input_image.pixels);
				for(alt_u32 i = 0; i < output_image.height; i++) {
					free((output_image.pixels)[i]);
				}
				free(output_image.pixels);
				// v2
                free(m2s_desc_copy);
                free(s2m_desc_copy);
                break;
            }

            // ----------------------------------------------------------------
			// printing formated report
            // ----------------------------------------------------------------
            perf_print_formatted_report(PERFORMANCE_COUNTER_BASE,
          		                            alt_get_cpu_freq(),
          		                                             2,
          		                          "sw_scale",
          		                          "hw_scale");

            // ----------------------------------------------------------------
            // free dynamic memory
			// ----------------------------------------------------------------
			for(alt_u32 i = 0; i < input_image.height; i++) {
				free((input_image.pixels)[i]);
			}
			free(input_image.pixels);
			for(alt_u32 i = 0; i < output_image.height; i++) {
				free((output_image.pixels)[i]);
			}
			free(output_image.pixels);
			// v2
			free(m2s_desc_copy);
			free(s2m_desc_copy);

			printf("\nProcessing success!!!\n\n");
            break;
        case '0':
        	printf("\nWARNING: PROGRAM ENDED!\n");
            exit(0);
            break;
        default:
            break;
        }
    }

    return 0;
}

