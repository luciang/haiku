#include "ColorUtils.h"
#include <stdlib.h>

/*!
	\brief An approximation of 31/255, which is needed for converting from 32-bit
		colors to 16-bit.
*/
#define RATIO_32_TO_16_BIT .121568627451

/*!
	\brief An approximation of 255/31, which is needed for converting from 16-bit
		colors to 32-bit.
*/
#define RATIO_16_TO_32_BIT 8.225806451613

/*!
	\brief Function for easy assignment of values to rgb_color objects
	\param col Pointer to an rgb_color
	\param r red value
	\param g green value
	\param b blue value
	\param a alpha value, defaults to 255
	
	This function will do nothing if given a NULL color pointer.
*/
void SetRGBColor(rgb_color *col,uint8 r, uint8 g, uint8 b, uint8 a)
{
	if(col)
	{
		col->red=r;
		col->green=g;
		col->blue=b;
		col->alpha=a;
	}
}

/*!
	\brief Function for easy conversion of 16-bit colors to 32-bit
	\param col Pointer to an rgb_color.
	\param color RGBA16 color
	
	This function will do nothing if passed a NULL 32-bit color.
*/
void SetRGBColor(rgb_color *col,uint16 color)
{
	if(!col)
		return;
	
	uint16 r16,g16,b16;
	
	// alpha's the easy part
	col->alpha=(color & 0x8000)?255:0;

	r16= (color >> 15) & 31;
	g16= (color >> 10) & 31;
	b16= color & 31;

	col->red=uint8(r16 * RATIO_16_TO_32_BIT);
	col->green=uint8(g16 * RATIO_16_TO_32_BIT);
	col->blue=uint8(b16 * RATIO_16_TO_32_BIT);
}

/*!
	\brief Function for easy conversion of 32-bit integer colors to an rgb_color structure
	\param col Pointer to an rgb_color.
	\param color 32-bit color as an integer
	
	This function will do nothing if passed a NULL 32-bit color.
*/
void SetRGBColor(rgb_color *col,uint32 color)
{
	if(!col)
		return;

	int8 *p8=(int8*)&color;
	col->blue=p8[0];
	col->red=p8[1];
	col->green=p8[2];
	col->alpha=p8[3];
}

/*!
	\brief Finds the index of the closest matching color in a rgb_color palette array
	\param palette Array of 256 rgb_color objects
	\param color Color to match
	\return Index of the closest matching color
	
	Note that passing a NULL palette will always return 0 and passing an array of less
	than 256 rgb_colors will cause a crash.
*/
uint8 FindClosestColor(rgb_color *palette, rgb_color color)
{
	if(!palette)
		return 0;
	
	uint16 cindex=0,cdelta=765,delta=765;
	rgb_color *c;
	
	for(uint16 i=0;i<256;i++)
	{
		c=&(palette[i]);
		delta=abs(c->red-color.red)+abs(c->green-color.green)+
			abs(c->blue-color.blue);

		if(delta==0)
		{
			cindex=i;
			break;
		}

		if(delta<cdelta)
		{
			cindex=i;
			cdelta=delta;
		}
	}

	return (uint8)cindex;
}

/*!
	\brief Constructs a RGBA15 color which best matches a given 32-bit color
	\param color Color to match
	\return The closest matching color's value
	
	Format is ARGB, 1:5:5:5
*/
uint16 FindClosestColor16(rgb_color color)
{
	uint16 r16,g16,b16;
	uint16 color16;
	
	r16=uint16(color.red * RATIO_32_TO_16_BIT);
	g16=uint16(color.green * RATIO_32_TO_16_BIT);
	b16=uint16(color.blue * RATIO_32_TO_16_BIT);

	// start with alpha value
	color16=(color.alpha>127)?0x8000:0;

	color16 |= r16 << 15;
	color16 |= g16 << 10;
	color16 |= b16;

	return color16;
}

// Function which could be used to . Position is
//  Any number outside
// this range will cause the function to fail and return the color (0,0,0,0)
// Alpha components are included in the calculations. 0 yields color #1
// and 1 yields color #2.

/*!
	\brief Function mostly for calculating gradient colors
	\param col Start color
	\param col2 End color
	\param position A floating point number such that 0.0 <= position <= 1.0. 0.0 results in the
		start color and 1.0 results in the end color.
	\return The blended color. If an invalid position was given, {0,0,0,0} is returned.
*/
rgb_color MakeBlendColor(rgb_color col, rgb_color col2, float position)
{
	rgb_color newcol={0,0,0,0};
	float mod=0;
	int16 delta;
	if(position<0 || position>1)
		return newcol;

	delta=int16(col2.red)-int16(col.red);
	mod=col.red + (position * delta);
	newcol.red=uint8(mod);
	if(mod>255 )
		newcol.red=255;
	if(mod<0 )
		newcol.red=0;

	delta=int16(col2.green)-int16(col.green);
	mod=col.green + (position * delta);
	newcol.green=uint8(mod);
	if(mod>255 )
		newcol.green=255;
	if(mod<0 )
		newcol.green=0;

	delta=int16(col2.blue)-int16(col.blue);
	mod=col.blue + (position * delta);
	newcol.blue=uint8(mod);
	if(mod>255 )
		newcol.blue=255;
	if(mod<0 )
		newcol.blue=0;

	delta=int8(col2.alpha)-int8(col.alpha);
	mod=col.alpha + (position * delta);
	newcol.alpha=uint8(mod);
	if(mod>255 )
		newcol.alpha=255;
	if(mod<0 )
		newcol.alpha=0;

	return newcol;
}
