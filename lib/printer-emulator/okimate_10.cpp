#include "okimate_10.h"
#include "../utils/utils.h"
#include "../../include/debug.h"

/**
 * Okimate 10 state machine
 * 
 * The Okimate 10 has both ESC and direct commands
 * Several commands have an additional argument and one has two arguments 
 * 
 * I use the epson_cmd state machine approach for both direct commands and ESC sequences.
 * There are two flags: cmdMode and escMode. cmdMode is set once a direct command with an argument is called.
 * escMode is set when ESC is received like normal. I chose to use the dot graphics like epson, too. This
 * allows mixed text and graphics in color. 
 * 
 * The colorMode is very different than anything else. Instead of printing char by char, three complete lines
 * of text and/or graphics will be buffered up as received. CMD and ESC sequences will be executed. For any
 * action that would normally write to the PDF immediately, a special COLOR case will be added to write to
 * an accompanying state array that parallels the buffer arrays. Once all three lines are received (CMY colors),
 * they will be printed in color. This will require a seperate loop to print. The colors will be chosen by 
 * comparing the character/graphics bytes across the CMY buffers and setting the color register appropriately.
 * If there's a characeter mismatch between buffers (except for SPACE), then I might just print the two chars 
 * in CMY order overlapping. Otherwise, the correct colors will be chosen.
 */
void okimate10::esc_not_implemented()
{
    uint8_t c = okimate_cmd.cmd;
    __IGNORE_UNUSED_VAR(c);
    Debug_printf("Command not implemented: ESC %u %x %c\n", c, c, c);
}

void okimate10::cmd_not_implemented(uint8_t c)
{
    __IGNORE_UNUSED_VAR(c);
    Debug_printf("Command not implemented: %u %x %c\n", c, c, c);
}

void okimate10::set_mode(uint8_t m)
{
    okimate_new_fnt_mask |= m;
}

void okimate10::clear_mode(uint8_t m)
{
    okimate_new_fnt_mask &= ~m;
}

void okimate10::reset_cmd()
{
    escMode = false;
    cmdMode = false;
    okimate_cmd.cmd = 0;
    okimate_cmd.ctr = 0;
    okimate_cmd.n = 0;
    okimate_cmd.data = 0;
}

void okimate10::okimate_handle_font()
{
    if ((okimate_current_fnt_mask != okimate_new_fnt_mask) || (okimate_new_fnt_mask & fnt_inverse))
    {
        fprintf(_file, ")]TJ\n ");
        // check and change typeface
        if (okimate_current_fnt_mask == 0xFF)
            fprintf(_file, "/F1 12 Tf ");
        if ((okimate_current_fnt_mask & 0x03) != (okimate_new_fnt_mask & 0x03))
            switch (okimate_new_fnt_mask & 0x03)
            {
            case 0: // normal
                fprintf(_file, "100 Tz");
                charWidth = 7.2; //72.0 / 10.0;
                break;
            case 1: // fine
                fprintf(_file, "60.606 Tz");
                charWidth = 72.0 / 16.5;
                break;
            case 2: // wide
                fprintf(_file, "200 Tz");
                charWidth = 14.4; //72.0 / 5.0;
                break;
            case 3: // bold
                fprintf(_file, "121.21 Tz");
                charWidth = 72.0 / 8.25;
                break;
            default:
                fprintf(_file, "100 Tz");
                charWidth = 7.2; //72.0 / 10.0;
                break;
            }
        // check and change color or reset font color when leaving REVERSE mode
        if (((okimate_current_fnt_mask & 0xF0) != (okimate_new_fnt_mask & 0xF0)) || ((okimate_current_fnt_mask & fnt_inverse) && !(okimate_new_fnt_mask & fnt_inverse)))
        {
            for (int i = 0; i < 4; i++)
            {
                fprintf(_file, " %d", (okimate_new_fnt_mask >> (i + 4) & 0x01));
            }
            fprintf(_file, " k ");
        }
        okimate_current_fnt_mask = okimate_new_fnt_mask;
        if (okimate_current_fnt_mask & fnt_inverse)
        {
            // make a rectangle "x y l w re f"
            for (int i = 0; i < 4; i++)
            {
                fprintf(_file, " %d", (okimate_current_fnt_mask >> (i + 4) & 0x01));
            }
            fprintf(_file, " k ");
            fprintf(_file, "%g %g %g 7 re f 0 0 0 0 k ", pdf_X + leftMargin, pdf_Y, charWidth);
        }
        fprintf(_file, " [(");
    }
}

uint16_t okimate10::okimate_cmd_ascii_to_int(uint8_t c)
{
    uint16_t N = okimate_cmd.n - 48;
    if (okimate_cmd.ctr == 1)
        return N;
    N *= 10;
    N += okimate_cmd.data - 48;
    if (okimate_cmd.ctr == 2)
        return N;
    N *= 10;
    N += c - 48;
    return N;
}

void okimate10::print_7bit_gfx(uint8_t c)
{
    // e.g., [(0)100(1)100(4)100(50)]TJ
    // lead with '0' to enter a space
    // then shift back with 100 and print each pin
    fprintf(_file, "0");
    for (int i = 0; i < 7; i++)
    {
        if ((c >> i) & 0x01)
            fprintf(_file, ")100(%u", i + 1);
    }
}

void okimate10::pdf_clear_modes()
{
    clear_mode(fnt_inverse); // implied by Atari manual page 28. Explicit in Commode manual page 26.
}

void okimate10::pdf_handle_char(uint8_t c, uint8_t aux1, uint8_t aux2)
{
    // Okimate 10 extras codes:
    // ESC CTRL-T ESC CTRL-N - 8.25 char/inch (0x14, 0x0E)
    // 0x99     Align Ribbon (for color mode)
    // 0x9B     EOL for color mode

    // 0x8A n    n/144" line advance (n * 1/2 pt vertial line feed)
    // 0x8C     form feed
    // ESC A - perforation skip OFF
    // ESC B - perforation skip ON

    // 146 0x92 - start REVERSE mode
    // 147 0x93 - stop REVERSE mode

    // ESC '%' - start graphics mode
    // 0x91 - stop graphics mode
    // 0x9A n data - repeat graphics data n times

    // 0x90 n - dot column horizontal tab

    if (escMode)
    {
        // Atari 1025 escape codes:
        // ESC CTRL-T - 16.5 char/inch        0x14
        // ESC CTRL-O - 10 char/inch          0x0F
        // ESC CTRL-N - 5 char/inch           0x0E
        // ESC L - long line 80 char/line     0x4C
        // ESC S - short line 64 char/line    0x53
        // ESC 6 - use 6 lines per inch       0x36
        // ESC 8 - use 8 lines per inch       0x38
        // ESC CTRL-W - start international   0x17 23
        // ESC CTRL-X - stop international    0x18 24

        // command state machine switching
        if (okimate_cmd.cmd == 0)
        {
            okimate_cmd.ctr = 0;
            okimate_cmd.cmd = c; // assign command char
#ifdef DEBUG
            Debug_printf("Command: %x\n", c);
#endif
        }
        else
        {
            okimate_cmd.ctr++; // increment counter to keep track of the byte in the command
#ifdef DEBUG
            Debug_printf("Command counter: %d\n", okimate_cmd.ctr);
#endif
        }

        if (okimate_cmd.ctr == 1)
        {
            okimate_cmd.n = c;
#ifdef DEBUG
            Debug_printf("n: %d\n", c);
#endif
        }
        else if (okimate_cmd.ctr == 2)
        {
            okimate_cmd.data = c;
#ifdef DEBUG
            Debug_printf("data: %d\n", c);
#endif
        }

        // state machine actions
        switch (okimate_cmd.cmd)
        {
        case 0x0E: // wide ESC & 14
            // change font to elongated like
            set_mode(fnt_expanded);
            reset_cmd();
            break;
        case 0x0F: // normal ESC & 15
            // change font to normal
            clear_mode(fnt_expanded | fnt_compressed);
            reset_cmd();
            break;
        case 0x14: // fine ESC & 20
            // change font to compressed
            clear_mode(fnt_expanded); // remove wide print mode - shown in Commod'e manual
            set_mode(fnt_compressed);
            reset_cmd();
            break;
        case 0x17: // 23
            intlFlag = true;
            reset_cmd();
            break;
        case 0x18: // 24
            intlFlag = false;
            reset_cmd();
            break;
        case 0x25: // 37, '%': // GRAPHICS MODE ON
            if (okimate_cmd.ctr == 0)
            {
                charWidth = 1.2;
                fprintf(_file, ")]TJ /F2 12 Tf 100 Tz [("); // set font to GFX mode
                fontUsed[1] = true;
                textMode = false;
#ifdef DEBUG
                Debug_printf("Entering GFX mode\n");
#endif
            }
            else
                switch (c)
                {
                case 0x91: // end gfx mode
                    // reset font
                    okimate_current_fnt_mask = 0xFF; // invalidate font mask
                    okimate_new_fnt_mask = 0x80;
                    okimate_handle_font();
                    textMode = true;
                    reset_cmd();
#ifdef DEBUG
                    Debug_printf("Finished GFX mode\n");
#endif
                    break;
                case 0x9A: // repeat gfx char n times
                    // toss control over to the Direct Command switch statement
                    escMode = false;
                    cmdMode = true;
                    okimate_cmd.cmd = 0x9A;
                    okimate_cmd.ctr = 0;
#ifdef DEBUG
                    Debug_printf("Go to repeated gfx char\n");
#endif
                    break;
                default:
                    print_7bit_gfx(c);
                    break;
                }
            break;
        case 0x36:             // '6'
            lineHeight = 12.0; //72.0/6.0;
            reset_cmd();
            break;
        case 0x38:            // '8'
            lineHeight = 9.0; //72.0/8.0;
            reset_cmd();
            break;
        case 0x41: // PERFORATION SKIP OFF
            bottomMargin = 0.0;
            topMargin = 0.0;
            reset_cmd();
            break;
        case 0x42: // PERFORATION SKIP ON
            bottomMargin = 72.0;
            topMargin = 72.0;
            reset_cmd();
            break;
        case 0x4c: // 'L'
            set_line_long();
            reset_cmd();
            break;
        case 0x53: // 'S'
            set_line_short();
            reset_cmd();
            break;
        default:
            reset_cmd();
            break;
        }
    }
    else if (cmdMode)
    {
        // command state machine switching
        if (okimate_cmd.ctr == 0)
        {
#ifdef DEBUG
            Debug_printf("Command: %c\n", okimate_cmd.cmd);
#endif
        }

        okimate_cmd.ctr++; // increment counter to keep track of the byte in the command
#ifdef DEBUG
        Debug_printf("Command counter: %d\n", okimate_cmd.ctr);
#endif

        if (okimate_cmd.ctr == 1)
        {
            okimate_cmd.n = c;
#ifdef DEBUG
            Debug_printf("n: %d\n", c);
#endif
        }
        else if (okimate_cmd.ctr == 2)
        {
            okimate_cmd.data = c;
#ifdef DEBUG
            Debug_printf("data: %d\n", c);
#endif
        }

        switch (okimate_cmd.cmd)
        {
        case 0x8A: // n/144" line advance (n * 1/2 pt vertial line feed)
            /* code */
            pdf_dY -= float(okimate_cmd.n) / 144.; // set pdf_dY and rise to fraction of line
            pdf_set_rise();
            reset_cmd();
            break;
        case 0x90: //0x90 n - dot column horizontal tab
            textMode = false;
            if ((c < 48) || (c > 57) || (okimate_cmd.ctr == 3))
            {
                uint16_t N = okimate_cmd_ascii_to_int(c);
                charWidth = 1.2;
                fprintf(_file, ")]TJ /F2 12 Tf 100 Tz [("); // set font to GFX mode
                fontUsed[1] = true;
                for (int i = 1; i < N; i++)
                {
                    fprintf(_file, " ");
                }
                okimate_new_fnt_mask = okimate_current_fnt_mask; // reset font
                okimate_current_fnt_mask = 0xFF;                 // invalidate font mask
                okimate_handle_font();
                textMode = true;
                reset_cmd();
            }
            break;
        case 0x9A: // 0x9A n data - repeat graphics data n times
            // receive control from the ESC-37 graphics mode
            if (okimate_cmd.ctr > 1)
            {
                for (int i = 0; i < okimate_cmd.n; i++)
                {
                    print_7bit_gfx(okimate_cmd.data);
                }
                // toss control back over to ESC-37 graphics mode
                cmdMode = false;
                escMode = true;
                okimate_cmd.cmd = 37; // graphics
                okimate_cmd.ctr = 1;
            }
            break;
        default:
            esc_not_implemented();
            reset_cmd();
            break;
        }
    }
    else
    {
        // Okimate 10 extras codes:
        // 0x99     Align Ribbon (for color mode)
        // 0x9B     EOL for color mode

        // 0x8A n    n/144" line advance (n * 1/2 pt vertial line feed)
        // 0x8C     form feed

        // 146, 0x92 - start REVERSE mode
        // 147, 0x93 - stop REVERSE mode

        // 0x91 - stop graphics mode
        // 0x9A n data - repeat graphics data n times

        // 0x90 n - dot column horizontal tab

        switch (c)
        {
        case 27:
            reset_cmd(); // clear command record to set up for fresh esc sequence
            escMode = true;
            break;
        case 0x8A: // n/144" line advance (n * 1/2 pt vertial line feed)
            cmdMode = true;
            okimate_cmd.cmd = c;
            okimate_cmd.ctr = 0;
            break;
        case 0x8C: // formfeed!
            pdf_end_page();
            pdf_new_page();
            break;
        case 0x90: // 0x90 n - dot column horizontal tab
            cmdMode = true;
            okimate_cmd.cmd = c;
            okimate_cmd.ctr = 0;
            break;
        // case 0x91: // not needed - implement in graphics handling in ESC mode state
        //     // stop graphics mode
        //     cmd_not_implemented(c);
        //     break;
        case 0x92:                 // start REVERSE mode
            set_mode(fnt_inverse); // see clear_modes(), reverse clears at EOL
            // cmd_not_implemented(c);
            break;
        case 0x93: // stop REVERSE mode
            clear_mode(fnt_inverse);
            // cmd_not_implemented(c);
            break;
        case 0x99: // 0x99     Align Ribbon (for color mode)
            cmd_not_implemented(c);
            colorMode = true;
            break;
        case 0x9A: // 0x9A n data - repeat graphics data n times
            cmdMode = true;
            okimate_cmd.cmd = c;
            okimate_cmd.ctr = 0;
            break;
        case 0x9B: // 0x9B     EOL for color mode
            if (colorMode)
                cmd_not_implemented(c);
            break;
        default:
            okimate_handle_font();
            print_char(c);
            break;
        }
    }
}

void okimate10::post_new_file()
{
    atari1025::post_new_file();
    shortname = "oki10";
    topMargin = 72.0;    // perf skip is default
    pdf_dY = 72.0;       // but start at top of first page
    bottomMargin = 72.0; // perf skip is default
}