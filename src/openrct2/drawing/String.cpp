#pragma region Copyright (c) 2014-2018 OpenRCT2 Developers
/*****************************************************************************
 * OpenRCT2, an open source clone of Roller Coaster Tycoon 2.
 *
 * OpenRCT2 is the work of many authors, a full list can be found in contributors.md
 * For more information, visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * A full copy of the GNU General Public License can be found in licence.txt
 *****************************************************************************/
#pragma endregion

#include <algorithm>
#include "../config/Config.h"
#include "../interface/Viewport.h"
#include "../localisation/Localisation.h"
#include "../platform/platform.h"
#include "../sprites.h"
#include "../util/Util.h"
#include "TTF.h"

enum {
    TEXT_DRAW_FLAG_INSET = 1 << 0,
    TEXT_DRAW_FLAG_OUTLINE = 1 << 1,
    TEXT_DRAW_FLAG_DARK = 1 << 2,
    TEXT_DRAW_FLAG_EXTRA_DARK = 1 << 3,
    TEXT_DRAW_FLAG_Y_OFFSET_EFFECT = 1 << 29,
    TEXT_DRAW_FLAG_TTF = 1 << 30,
    TEXT_DRAW_FLAG_NO_DRAW = 1u << 31
};

static sint32 ttf_get_string_width(const utf8 *text);

/**
 *
 *  rct2: 0x006C23B1
 */
sint32 gfx_get_string_width_new_lined(utf8 *text)
{
    utf8 *ch = text;
    utf8 *firstCh = text;
    utf8 *nextCh;
    utf8 backup;
    sint32 codepoint;

    sint32 maxWidth = 0;
    while ((codepoint = utf8_get_next(ch, (const utf8**)&nextCh)) != 0) {
        if (codepoint == FORMAT_NEWLINE || codepoint == FORMAT_NEWLINE_SMALLER) {
            backup = *nextCh;
            *nextCh = 0;
            maxWidth = std::max(maxWidth, gfx_get_string_width(firstCh));
            *nextCh = backup;
            firstCh = nextCh;
        }
        ch = nextCh;
    }
    maxWidth = std::max(maxWidth, gfx_get_string_width(firstCh));

    return maxWidth;
}

/**
 * Return the width of the string in buffer
 *
 *  rct2: 0x006C2321
 * buffer (esi)
 */
sint32 gfx_get_string_width(const utf8 * buffer)
{
    return ttf_get_string_width(buffer);
}

/**
 * Clip the text in buffer to width, add ellipsis and return the new width of the clipped string
 *
 *  rct2: 0x006C2460
 * buffer (esi)
 * width (edi)
 */
sint32 gfx_clip_string(utf8 *text, sint32 width)
{
    sint32 clippedWidth;

    if (width < 6) {
        *text = 0;
        return 0;
    }

    clippedWidth = gfx_get_string_width(text);
    if (clippedWidth <= width) {
        return clippedWidth;
    }

    utf8 backup[4];
    utf8 *ch = text;
    utf8 *nextCh = text;
    utf8 *clipCh = text;
    sint32 codepoint;
    while ((codepoint = utf8_get_next(ch, (const utf8**)&nextCh)) != 0) {
        if (utf8_is_format_code(codepoint)) {
            ch = nextCh;
            ch += utf8_get_format_code_arg_length(codepoint);
            continue;
        }

        for (sint32 i = 0; i < 4; i++) { backup[i] = nextCh[i]; };
        for (sint32 i = 0; i < 3; i++) { nextCh[i] = '.'; }
        nextCh[3] = 0;

        sint32 queryWidth = gfx_get_string_width(text);
        if (queryWidth < width) {
            clipCh = nextCh;
            clippedWidth = queryWidth;
        } else {
            for (sint32 i = 0; i < 3; i++) { clipCh[i] = '.'; }
            clipCh[3] = 0;
            return clippedWidth;
        }

        for (sint32 i = 0; i < 4; i++) { nextCh[i] = backup[i]; };
        ch = nextCh;
    }
    return gfx_get_string_width(text);
}

/**
 * Wrap the text in buffer to width, returns width of longest line.
 *
 * Inserts NULL where line should break (as \n is used for something else),
 * so the number of lines is returned in num_lines. font_height seems to be
 * a control character for line height.
 *
 *  rct2: 0x006C21E2
 * buffer (esi)
 * width (edi) - in
 * num_lines (edi) - out
 * font_height (ebx) - out
 */
sint32 gfx_wrap_string(utf8 *text, sint32 width, sint32 *outNumLines, sint32 *outFontHeight)
{
    sint32 lineWidth = 0;
    sint32 maxWidth = 0;
    *outNumLines = 0;

    // Pointer to the start of the current word
    utf8 *currentWord = nullptr;

    // Width of line up to current word
    sint32 currentWidth = 0;

    utf8 *ch = text;
    utf8 *firstCh = text;
    utf8 *nextCh;
    sint32 codepoint;
    sint32 numCharactersOnLine = 0;
    while ((codepoint = utf8_get_next(ch, (const utf8**)&nextCh)) != 0) {
        if (codepoint == ' ') {
            currentWord = ch;
            currentWidth = lineWidth;
            numCharactersOnLine++;
        } else if (codepoint == FORMAT_NEWLINE) {
            *ch++ = 0;
            maxWidth = std::max(maxWidth, lineWidth);
            (*outNumLines)++;
            lineWidth = 0;
            currentWord = nullptr;
            firstCh = ch;
            numCharactersOnLine = 0;
            continue;
        } else if (utf8_is_format_code(codepoint)) {
            ch = nextCh;
            ch += utf8_get_format_code_arg_length(codepoint);
            continue;
        }

        uint8 saveCh = *nextCh;
        *nextCh = 0;
        lineWidth = gfx_get_string_width(firstCh);
        *nextCh = saveCh;

        if (lineWidth <= width || numCharactersOnLine == 0) {
            ch = nextCh;
            numCharactersOnLine++;
        } else if (currentWord == nullptr) {
            // Single word is longer than line, insert null terminator
            ch += utf8_insert_codepoint(ch, 0);
            maxWidth = std::max(maxWidth, lineWidth);
            (*outNumLines)++;
            lineWidth = 0;
            currentWord = nullptr;
            firstCh = ch;
            numCharactersOnLine = 0;
        } else {
            ch = currentWord;
            *ch++ = 0;

            maxWidth = std::max(maxWidth, currentWidth);
            (*outNumLines)++;
            lineWidth = 0;
            currentWord = nullptr;
            firstCh = ch;
            numCharactersOnLine = 0;
        }
    }
    maxWidth = std::max(maxWidth, lineWidth);
    *outFontHeight = gCurrentFontSpriteBase;
    return maxWidth == 0 ? lineWidth : maxWidth;
}

/**
 * Draws text that is left aligned and vertically centred.
 */
void gfx_draw_string_left_centred(rct_drawpixelinfo *dpi, rct_string_id format, void *args, sint32 colour, sint32 x, sint32 y)
{
    gCurrentFontSpriteBase = FONT_SPRITE_BASE_MEDIUM;
    char *buffer = gCommonStringFormatBuffer;
    format_string(buffer, 256, format, args);
    sint32 height = string_get_height_raw(buffer);
    gfx_draw_string(dpi, buffer, colour, x, y - (height / 2));
}

/**
 * Changes the palette so that the next character changes colour
 */
static void colour_char(uint8 colour, const uint16* current_font_flags, uint8* palette_pointer) {

    sint32 colour32 = 0;
    const rct_g1_element * g1 = gfx_get_g1_element(SPR_TEXT_PALETTE);
    if (g1 != nullptr)
    {
        colour32 = bswap(((uint32 *)g1->offset)[colour & 0xFF]);
    }

    if (!(*current_font_flags & 2))
    {
        colour32 = colour32 & 0x0FF0000FF;
    }
    // Adjust text palette. Store current colour?
    palette_pointer[1] = colour32 & 0xFF;
    palette_pointer[2] = (colour32 >> 8) & 0xFF;
    palette_pointer[3] = (colour32 >> 16) & 0xFF;
    palette_pointer[4] = (colour32 >> 24) & 0xFF;
}

/**
 * Changes the palette so that the next character changes colour
 * This is specific to changing to a predefined window related colour
 */
static void colour_char_window(uint8 colour, const uint16* current_font_flags,uint8* palette_pointer) {

    sint32 eax;

    colour = NOT_TRANSLUCENT(colour);
    eax = ColourMapA[colour].colour_11;
    if (*current_font_flags & TEXT_DRAW_FLAG_OUTLINE) {
        eax |= 0x0A0A00;
    }
     //Adjust text palette. Store current colour?
    palette_pointer[1] = eax & 0xFF;
    palette_pointer[2] = (eax >> 8) & 0xFF;
    palette_pointer[3] = (eax >> 16) & 0xFF;
    palette_pointer[4] = (eax >> 24) & 0xFF;
}

/**
 *
 *  rct2: 0x006C1DB7
 *
 * left     : cx
 * top      : dx
 * numLines : bp
 * text     : esi
 * dpi      : edi
 */
void draw_string_centred_raw(rct_drawpixelinfo *dpi, sint32 x, sint32 y, sint32 numLines, char *text)
{
    gCurrentFontSpriteBase = FONT_SPRITE_BASE_MEDIUM;
    gfx_draw_string(dpi, (char *)"", COLOUR_BLACK, dpi->x, dpi->y);
    gCurrentFontFlags = 0;

    for (sint32 i = 0; i <= numLines; i++) {
        sint32 width = gfx_get_string_width(text);
        gfx_draw_string(dpi, text, TEXT_COLOUR_254, x - (width / 2), y);

        const utf8 *ch = text;
        const utf8 *nextCh = nullptr;

        while ((utf8_get_next(ch, &nextCh)) != 0) {
            ch = nextCh;
        }
        text = (char*)(ch + 1);

        y += font_get_line_height(gCurrentFontSpriteBase);
    }
}

sint32 string_get_height_raw(char *buffer)
{
    uint16 fontBase = gCurrentFontSpriteBase;

    sint32 height = 0;
    if (fontBase <= FONT_SPRITE_BASE_MEDIUM)
        height += 10;
    else if (fontBase == FONT_SPRITE_BASE_TINY)
        height += 6;

    char *ch = buffer;
    while (*ch != 0) {
        char c = *ch++;
        switch (c) {
        case FORMAT_NEWLINE:
            if (fontBase == FONT_SPRITE_BASE_SMALL || fontBase == FONT_SPRITE_BASE_MEDIUM) {
                height += 10;
                break;
            } else if (fontBase == FONT_SPRITE_BASE_TINY) {
                height += 6;
                break;
            }
            height += 18;
            break;
        case FORMAT_NEWLINE_SMALLER:
            if (fontBase == FONT_SPRITE_BASE_SMALL || fontBase == FONT_SPRITE_BASE_MEDIUM) {
                height += 5;
                break;
            } else if (fontBase == FONT_SPRITE_BASE_TINY) {
                height += 3;
                break;
            }
            height += 9;
            break;
        case FORMAT_TINYFONT:
            fontBase = FONT_SPRITE_BASE_TINY;
            break;
        case FORMAT_BIGFONT:
            fontBase = FONT_SPRITE_BASE_BIG;
            break;
        case FORMAT_MEDIUMFONT:
            fontBase = FONT_SPRITE_BASE_MEDIUM;
            break;
        case FORMAT_SMALLFONT:
            fontBase = FONT_SPRITE_BASE_SMALL;
            break;
        default:
            if (c >= 32) continue;
            if (c <= 4) {
                ch++;
                continue;
            }
            if (c <= 16) continue;
            ch += 2;
            if (c <= 22) continue;
            ch += 2;
            break;
        }
    }

    return height;
}

/**
 *
 *  rct2: 0x006C1F57
 *
 * colour   : al
 * format   : bx
 * x        : cx
 * y        : dx
 * text     : esi
 * dpi      : edi
 * width    : bp
 * ticks    : ebp >> 16
 */
void gfx_draw_string_centred_wrapped_partial(rct_drawpixelinfo *dpi, sint32 x, sint32 y, sint32 width, sint32 colour, rct_string_id format, void *args, sint32 ticks)
{
    sint32 numLines, fontSpriteBase, lineHeight, lineY;
    utf8 *buffer = gCommonStringFormatBuffer;

    gCurrentFontSpriteBase = FONT_SPRITE_BASE_MEDIUM;
    gfx_draw_string(dpi, (char *)"", colour, dpi->x, dpi->y);
    format_string(buffer, 256, format, args);


    gCurrentFontSpriteBase = FONT_SPRITE_BASE_MEDIUM;
    gfx_wrap_string(buffer, width, &numLines, &fontSpriteBase);
    lineHeight = font_get_line_height(fontSpriteBase);

    sint32 numCharactersDrawn = 0;
    sint32 numCharactersToDraw = ticks;

    gCurrentFontFlags = 0;
    lineY = y - ((numLines * lineHeight) / 2);
    for (sint32 line = 0; line <= numLines; line++) {
        sint32 halfWidth = gfx_get_string_width(buffer) / 2;

        utf8 *ch = buffer;
        utf8 *nextCh;
        sint32 codepoint;
        while ((codepoint = utf8_get_next(ch, (const utf8**)&nextCh)) != 0) {
            if (!utf8_is_format_code(codepoint)) {
                numCharactersDrawn++;
                if (numCharactersDrawn > numCharactersToDraw) {
                    *ch = 0;
                    break;
                }
            }
            ch = nextCh;
        }

        gfx_draw_string(dpi, buffer, TEXT_COLOUR_254, x - halfWidth, lineY);

        if (numCharactersDrawn > numCharactersToDraw) {
            break;
        }

        buffer = get_string_end(buffer) + 1;
        lineY += lineHeight;
    }
}

struct text_draw_info {
    sint32 startX;
    sint32 startY;
    sint32 x;
    sint32 y;
    sint32 maxX;
    sint32 maxY;
    sint32 flags;
    uint8 palette[8];
    uint16 font_sprite_base;
    const sint8 *y_offset;
};

static void ttf_draw_character_sprite(rct_drawpixelinfo *dpi, sint32 codepoint, text_draw_info *info)
{
    sint32 characterWidth = font_sprite_get_codepoint_width(info->font_sprite_base, codepoint);
    sint32 sprite = font_sprite_get_codepoint_sprite(info->font_sprite_base, codepoint);

    if (!(info->flags & TEXT_DRAW_FLAG_NO_DRAW)) {

        sint32 x = info->x;
        sint32 y = info->y;
        if (info->flags & TEXT_DRAW_FLAG_Y_OFFSET_EFFECT) {
            y += *info->y_offset++;
        }
        gfx_draw_glpyh(dpi, sprite, x, y, info->palette);
    }

    info->x += characterWidth;
}

static void ttf_draw_string_raw_sprite(rct_drawpixelinfo *dpi, const utf8 *text, text_draw_info *info)
{
    const utf8 *ch = text;
    sint32 codepoint;

    while (!utf8_is_format_code(codepoint = utf8_get_next(ch, &ch))) {
        ttf_draw_character_sprite(dpi, codepoint, info);
    };
}

#ifndef NO_TTF

static void ttf_draw_string_raw_ttf(rct_drawpixelinfo *dpi, const utf8 *text, text_draw_info *info)
{
    if (!ttf_initialise())
        return;

    TTFFontDescriptor *fontDesc = ttf_get_font_from_sprite_base(info->font_sprite_base);
    if (fontDesc->font == nullptr) {
        ttf_draw_string_raw_sprite(dpi, text, info);
        return;
    }

    if (info->flags & TEXT_DRAW_FLAG_NO_DRAW) {
        info->x += ttf_getwidth_cache_get_or_add(fontDesc->font, text);
        return;
    } else {
        uint8 colour = info->palette[1];
        TTFSurface * surface = ttf_surface_cache_get_or_add(fontDesc->font, text);
        if (surface == nullptr)
            return;

        sint32 drawX = info->x + fontDesc->offset_x;
        sint32 drawY = info->y + fontDesc->offset_y;
        sint32 width = surface->w;
        sint32 height = surface->h;

        sint32 overflowX = (dpi->x + dpi->width) - (drawX + width);
        sint32 overflowY = (dpi->y + dpi->height) - (drawY + height);
        if (overflowX < 0) width += overflowX;
        if (overflowY < 0) height += overflowY;
        sint32 skipX = drawX - dpi->x;
        sint32 skipY = drawY - dpi->y;
        info->x += width;

        auto src = (const uint8 *)surface->pixels;
        uint8 *dst = dpi->bits;

        if (skipX < 0) {
            width += skipX;
            src += -skipX;
            skipX = 0;
        }
        if (skipY < 0) {
            height += skipY;
            src += (-skipY * surface->pitch);
            skipY = 0;
        }

        dst += skipX;
        dst += skipY * (dpi->width + dpi->pitch);

        sint32 srcScanSkip = surface->pitch - width;
        sint32 dstScanSkip = dpi->width + dpi->pitch - width;
        uint8 *dst_orig = dst;
        const uint8 *src_orig = src;

        // Draw shadow/outline
        if (info->flags & TEXT_DRAW_FLAG_OUTLINE) {
            for (sint32 yy = 0; yy < height - 0; yy++) {
                for (sint32 xx = 0; xx < width - 0; xx++) {
                    if (*src != 0) {
                        *(dst + 1) = info->palette[3]; // right
                        *(dst - 1) = info->palette[3]; // left
                        *(dst - width - dstScanSkip) = info->palette[3]; // top
                        *(dst + width + dstScanSkip) = info->palette[3]; // bottom
                    }
                    src++;
                    dst++;
                }
                // Skip any remaining bits
                src += srcScanSkip;
                dst += dstScanSkip;
            }
        }
        {
            dst = dst_orig;
            src = src_orig;
            bool use_hinting = gConfigFonts.enable_hinting && fontDesc->hinting_threshold > 0;
            for (sint32 yy = 0; yy < height; yy++) {
                for (sint32 xx = 0; xx < width; xx++) {
                    if (*src != 0) {
                        if (info->flags & TEXT_DRAW_FLAG_INSET) {
                            *(dst + width + dstScanSkip + 1) = info->palette[3];
                        }

                        if (*src > 180 || !use_hinting)
                        {
                            // Centre of the glyph: use full colour.
                            *dst = colour;
                        }
                        else if (use_hinting && *src > fontDesc->hinting_threshold)
                        {
                            // Simulate font hinting by shading the background colour instead.
                            if (info->flags & TEXT_DRAW_FLAG_OUTLINE)
                            {
                                // As outlines are black, these texts should always use a darker shade
                                // of the foreground colour for font hinting.
                                *dst = blendColours(colour, PALETTE_INDEX_0);
                            }
                            else
                            {
                                *dst = blendColours(colour, *dst);
                            }
                        }
                    }
                    src++;
                    dst++;
                }
                src += srcScanSkip;
                dst += dstScanSkip;
            }
        }
    }
}

#endif // NO_TTF

static void ttf_draw_string_raw(rct_drawpixelinfo *dpi, const utf8 *text, text_draw_info *info)
{
#ifndef NO_TTF
    if (info->flags & TEXT_DRAW_FLAG_TTF) {
        ttf_draw_string_raw_ttf(dpi, text, info);
    } else {
#endif // NO_TTF
        ttf_draw_string_raw_sprite(dpi, text, info);
#ifndef NO_TTF
    }
#endif // NO_TTF
}

static const utf8 *ttf_process_format_code(rct_drawpixelinfo *dpi, const utf8 *text, text_draw_info *info)
{
    const utf8 *nextCh;
    sint32 codepoint;

    codepoint = utf8_get_next(text, &nextCh);
    switch (codepoint) {
    case FORMAT_MOVE_X:
        info->x = info->startX + (uint8)(*nextCh++);
        break;
    case FORMAT_ADJUST_PALETTE:
    {
        uint16 eax = palette_to_g1_offset[(uint8)*nextCh++];
        const rct_g1_element * g1 = gfx_get_g1_element(eax);
        if (g1 != nullptr)
        {
            uint32 ebx = g1->offset[249] + 256;
            if (!(info->flags & TEXT_DRAW_FLAG_OUTLINE)) {
                ebx = ebx & 0xFF;
            }
            info->palette[1] = ebx & 0xFF;
            info->palette[2] = (ebx >> 8) & 0xFF;

            // Adjust the text palette
            memcpy(info->palette + 3, &(g1->offset[247]), 2);
            memcpy(info->palette + 5, &(g1->offset[250]), 2);
        }
        break;
    }
    case FORMAT_3:
    case FORMAT_4:
        nextCh++;
        break;
    case FORMAT_NEWLINE:
        info->x = info->startX;
        info->y += font_get_line_height(info->font_sprite_base);
        break;
    case FORMAT_NEWLINE_SMALLER:
        info->x = info->startX;
        info->y += font_get_line_height_small(info->font_sprite_base);
        break;
    case FORMAT_TINYFONT:
        info->font_sprite_base = FONT_SPRITE_BASE_TINY;
        break;
    case FORMAT_SMALLFONT:
        info->font_sprite_base = FONT_SPRITE_BASE_SMALL;
        break;
    case FORMAT_MEDIUMFONT:
        info->font_sprite_base = FONT_SPRITE_BASE_MEDIUM;
        break;
    case FORMAT_BIGFONT:
        info->font_sprite_base = 672;
        break;
    case FORMAT_OUTLINE:
        info->flags |= TEXT_DRAW_FLAG_OUTLINE;
        break;
    case FORMAT_OUTLINE_OFF:
        info->flags &= ~TEXT_DRAW_FLAG_OUTLINE;
        break;
    case FORMAT_WINDOW_COLOUR_1:
    {
        uint16 flags = info->flags;
        colour_char_window(gCurrentWindowColours[0], &flags, info->palette);
        break;
    }
    case FORMAT_WINDOW_COLOUR_2:
    {
        uint16 flags = info->flags;
        colour_char_window(gCurrentWindowColours[1], &flags, info->palette);
        break;
    }
    case FORMAT_WINDOW_COLOUR_3:
    {
        uint16 flags = info->flags;
        colour_char_window(gCurrentWindowColours[2], &flags, info->palette);
        break;
    }
    case FORMAT_16:
        break;
    case FORMAT_INLINE_SPRITE:
    {
        uint32 imageId = *((uint32*)(nextCh));
        const rct_g1_element * g1 = gfx_get_g1_element(imageId & 0x7FFFF);
        if (g1 != nullptr)
        {
            if (!(info->flags & TEXT_DRAW_FLAG_NO_DRAW)) {
                gfx_draw_sprite(dpi, imageId, info->x, info->y, 0);
            }
            info->x += g1->width;
        }
        nextCh += 4;
        break;
    }
    default:
        if (codepoint >= FORMAT_COLOUR_CODE_START && codepoint <= FORMAT_COLOUR_CODE_END) {
            uint16 flags = info->flags;
            colour_char(codepoint - FORMAT_COLOUR_CODE_START, &flags, info->palette);
        } else if (codepoint <= 0x16) { //case 0x11? FORMAT_NEW_LINE_X_Y
            nextCh += 2;
        } else {
            nextCh += 4;//never happens?
        }
        break;
    }
    return nextCh;
}

static const utf8 *ttf_process_glyph_run(rct_drawpixelinfo *dpi, const utf8 *text, text_draw_info *info)
{
    utf8 buffer[512];
    const utf8 *ch = text;
    const utf8 *lastCh;
    sint32 codepoint;

#ifndef NO_TTF
    bool isTTF = info->flags & TEXT_DRAW_FLAG_TTF;
#else
    bool isTTF = false;
#endif // NO_TTF
    while (!utf8_is_format_code(codepoint = utf8_get_next(ch, &lastCh))) {
        if (isTTF && utf8_should_use_sprite_for_codepoint(codepoint)) {
            break;
        }
        ch = lastCh;
    }
    if (codepoint == 0) {
        ttf_draw_string_raw(dpi, text, info);
        return ch;
    } else {
        size_t length = (size_t)(ch - text);
        memcpy(buffer, text, length);
        buffer[length] = 0;
        ttf_draw_string_raw(dpi, buffer, info);
        return ch;
    }
}

static void ttf_process_string(rct_drawpixelinfo *dpi, const utf8 *text, text_draw_info *info)
{
    const utf8 *ch = text;
    const utf8 *nextCh;
    sint32 codepoint;

#ifndef NO_TTF
    bool isTTF = info->flags & TEXT_DRAW_FLAG_TTF;
#else
    bool isTTF = false;
#endif // NO_TTF

    while ((codepoint = utf8_get_next(ch, &nextCh)) != 0) {
        if (utf8_is_format_code(codepoint)) {
            ch = ttf_process_format_code(dpi, ch, info);
        } else if (isTTF && utf8_should_use_sprite_for_codepoint(codepoint)) {
            ttf_draw_character_sprite(dpi, codepoint, info);
            ch = nextCh;
        } else {
            ch = ttf_process_glyph_run(dpi, ch, info);
        }
        info->maxX = std::max(info->maxX, info->x);
        info->maxY = std::max(info->maxY, info->y);
    }
}

static void ttf_process_initial_colour(sint32 colour, text_draw_info *info)
{
    if (colour != TEXT_COLOUR_254 && colour != TEXT_COLOUR_255) {
        info->flags &= ~(TEXT_DRAW_FLAG_INSET | TEXT_DRAW_FLAG_OUTLINE | TEXT_DRAW_FLAG_DARK | TEXT_DRAW_FLAG_EXTRA_DARK);
        if ((sint16)info->font_sprite_base < 0) {
            info->flags |= TEXT_DRAW_FLAG_DARK;
            if ((sint16)info->font_sprite_base == FONT_SPRITE_BASE_MEDIUM_EXTRA_DARK) {
                info->flags |= TEXT_DRAW_FLAG_EXTRA_DARK;
            }
            info->font_sprite_base = FONT_SPRITE_BASE_MEDIUM;
        }
        if (colour & COLOUR_FLAG_OUTLINE) {
            info->flags |= TEXT_DRAW_FLAG_OUTLINE;
        }
        colour &= ~COLOUR_FLAG_OUTLINE;
        if (!(colour & COLOUR_FLAG_INSET)) {
            if (!(info->flags & TEXT_DRAW_FLAG_INSET)) {
                uint16 flags = info->flags;
                colour_char_window(colour, &flags, (uint8*)&info->palette);
            }
        } else {
            info->flags |= TEXT_DRAW_FLAG_INSET;
            colour &= ~COLOUR_FLAG_INSET;

            uint32 eax;
            if (info->flags & TEXT_DRAW_FLAG_DARK) {
                if (info->flags & TEXT_DRAW_FLAG_EXTRA_DARK) {
                    eax = ColourMapA[colour].mid_light;
                    eax = eax << 16;
                    eax = eax | ColourMapA[colour].dark;
                } else {
                    eax = ColourMapA[colour].light;
                    eax = eax << 16;
                    eax = eax | ColourMapA[colour].mid_dark;
                }
            } else {
                eax = ColourMapA[colour].lighter;
                eax = eax << 16;
                eax = eax | ColourMapA[colour].mid_light;
            }

            // Adjust text palette. Store current colour? ;
            info->palette[1] = eax & 0xFF;
            info->palette[2] = (eax >> 8) & 0xFF;
            info->palette[3] = (eax >> 16) & 0xFF;
            info->palette[4] = (eax >> 24) & 0xFF;
            eax = 0;
        }
    }
}

void ttf_draw_string(rct_drawpixelinfo *dpi, const_utf8string text, sint32 colour, sint32 x, sint32 y)
{
    if (text == nullptr) return;

    text_draw_info info;
    info.font_sprite_base = gCurrentFontSpriteBase;
    info.flags = gCurrentFontFlags;
    info.startX = x;
    info.startY = x;
    info.x = x;
    info.y = y;

    if (gUseTrueTypeFont) {
        info.flags |= TEXT_DRAW_FLAG_TTF;
    }

    memcpy(info.palette, text_palette, sizeof(info.palette));
    ttf_process_initial_colour(colour, &info);
    ttf_process_string(dpi, text, &info);
    memcpy(text_palette, info.palette, sizeof(info.palette));

    gCurrentFontSpriteBase = info.font_sprite_base;
    gCurrentFontFlags = info.flags;

    gLastDrawStringX = info.x;
    gLastDrawStringY = info.y;
}

static sint32 ttf_get_string_width(const utf8 *text)
{
    text_draw_info info;
    info.font_sprite_base = gCurrentFontSpriteBase;
    info.flags = gCurrentFontFlags;
    info.startX = 0;
    info.startY = 0;
    info.x = 0;
    info.y = 0;
    info.maxX = 0;
    info.maxY = 0;

    info.flags |= TEXT_DRAW_FLAG_NO_DRAW;
    if (gUseTrueTypeFont) {
        info.flags |= TEXT_DRAW_FLAG_TTF;
    }

    ttf_process_string(nullptr, text, &info);

    return info.maxX;
}

/**
 *
 *  rct2: 0x00682F28
 */
void gfx_draw_string_with_y_offsets(rct_drawpixelinfo *dpi, const utf8 *text, sint32 colour, sint32 x, sint32 y, const sint8 *yOffsets, bool forceSpriteFont)
{
    text_draw_info info;
    info.font_sprite_base = gCurrentFontSpriteBase;
    info.flags = gCurrentFontFlags;
    info.startX = x;
    info.startY = x;
    info.x = x;
    info.y = y;
    info.y_offset = yOffsets;

    info.flags |= TEXT_DRAW_FLAG_Y_OFFSET_EFFECT;

    if (!forceSpriteFont && gUseTrueTypeFont) {
        info.flags |= TEXT_DRAW_FLAG_TTF;
    }

    memcpy(info.palette, text_palette, sizeof(info.palette));
    ttf_process_initial_colour(colour, &info);
    ttf_process_string(dpi, text, &info);
    memcpy(text_palette, info.palette, sizeof(info.palette));

    gCurrentFontSpriteBase = info.font_sprite_base;
    gCurrentFontFlags = info.flags;

    gLastDrawStringX = info.x;
    gLastDrawStringY = info.y;
}

void shorten_path(utf8 *buffer, size_t bufferSize, const utf8 *path, sint32 availableWidth)
{
    size_t length = strlen(path);

    // Return full string if it fits
    if (gfx_get_string_width((char*)path) <= availableWidth) {
        safe_strcpy(buffer, path, bufferSize);
        return;
    }

    // Count path separators
    sint32 path_separators = 0;
    for (size_t x = 0; x < length; x++) {
        if (path[x] == *PATH_SEPARATOR || path[x] == '/') {
            path_separators++;
        }
    }

    // TODO: Replace with unicode ellipsis when supported
    safe_strcpy(buffer, "...", bufferSize);

    // Abbreviate beginning with xth separator
    sint32 begin = -1;
    for (sint32 x = 0; x < path_separators; x++){
        do {
            begin++;
        } while (path[begin] != *PATH_SEPARATOR && path[begin] != '/');

        safe_strcpy(buffer + 3, path + begin, bufferSize - 3);
        if (gfx_get_string_width(buffer) <= availableWidth) {
            return;
        }
    }

    safe_strcpy(buffer, path, bufferSize);
}
