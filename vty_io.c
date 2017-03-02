#include "vty_io.h"
#include <readline/readline.h>
#include <readline/history.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include "http_server.h"
#include "socket_io.h"
#include <arpa/telnet.h>

void    file_write_cb(vty_t* vty, const char* buf, size_t len);
char*   file_read_cb(vty_t* vty);
void    std_write_cb(vty_t* vty, const char* buf, size_t len);
char*   std_read_cb(vty_t* vty);
void    std_show_history(vty_t* vty);

char*   http_read_cb(vty_t* vty);
void    http_write_cb(vty_t* vty, const char* buf, size_t len);
void    http_flush_cb(vty_t* vty);

vty_t*  vty_io_create(vty_type type, vty_data_t* data)
{
    vty_t* new_vty = vty_create(type, data);
    vty_io_config(new_vty);
    return new_vty;
}

void    vty_io_config(vty_t* vty)
{
    switch(vty->type)
    {
    case VTY_FILE:
        //rl_instream = vty->data->desc.file;
        //rl_outstream = vty->data->desc.file;
        vty->write_cb = file_write_cb;
        vty->read_cb = file_read_cb;
        //vty->buffer = calloc(BUFSIZE, sizeof(char));
        break;
    case VTY_STD:
        //rl_callback_handler_install (vty->prompt, cb_linehandler);
        rl_instream = vty->data->desc.io_pair[IN];
        rl_outstream = vty->data->desc.io_pair[OUT];
        vty->write_cb = std_write_cb;
        vty->read_cb = std_read_cb;
        vty->show_history_cb = std_show_history;

        //vty->buffer = calloc(BUFSIZE, sizeof(char));
        break;
    case VTY_HTTP:
        vty->write_cb = http_write_cb;
        vty->read_cb = http_read_cb;
        vty->flush_cb = http_flush_cb;
        //vty->buffer = calloc(BUFSIZE, sizeof(char));
        break;
    case VTY_SOCKET:
        vty->write_cb = socket_write_cb;
        vty->read_cb = socket_read_cb;
        vty->flush_cb = socket_flush_cb;
        vty->erase_char_cb = socket_erase_cb;
        vty->erase_line_cb = socket_erase_line_cb;
        vty->cursor_left_cb = socket_cursor_left_cb;
        vty->cursor_right_cb = socket_cursor_right_cb;

        //char iac_sga_buf[3] = {255, 251, 3};
        /*
        unsigned char cmd_will_echo[] = { IAC, WILL, TELOPT_ECHO };
        unsigned char cmd_will_sga[] = { IAC, WILL, TELOPT_SGA };
        unsigned char cmd_dont_linemode[] = { IAC, DONT, TELOPT_LINEMODE };
        
        socket_write_cb(vty, cmd_will_echo, 3);
        socket_write_cb(vty, cmd_will_sga, 3);
        socket_write_cb(vty, cmd_dont_linemode, 3);
        */
        //vty->buffer = calloc(BUFSIZE, sizeof(char));
        break;
    }
}


void    file_write_cb(vty_t* vty, const char* buf, size_t len)
{
    fprintf(vty->data->desc.file, buf);
}

char*   file_read_cb(vty_t* vty)
{
    if(vty->multi_line)
    {
        char ch = 0;
        memset(vty->buffer, 0, vty->buf_size);
        vty->buf_size = 0;
        while(true)
        {
            // Read multiple lines of input into buffer until \n.\n is found
            ch = fgetc(vty->data->desc.file);
            if(ch == vty->multiline_stop_char)
            {
                break;
            }
            if(vty->buf_size + 1 < BUFSIZE)
            {
                vty->buffer[vty->buf_size++] = ch;
            }
        }
        return vty->buffer;
    }
    else
    {
        char* buffer = calloc(2048, sizeof(char));
        char* res = fgets(buffer, 2048, vty->data->desc.file);
    
        if(NULL == res)
        {
            free(buffer);
        }
    
        return res;
    }
}

void    std_write_cb(vty_t* vty, const char* buf, size_t len)
{
    write(fileno(vty->data->desc.io_pair[OUT]), buf, len);
}

char*   std_read_cb(vty_t* vty)
{
    /*fd_set fds;
    keep_running = true;
    
    rl_set_prompt(vty->prompt);
    while(keep_running)
    {
        FD_ZERO (&fds);
        FD_SET(fileno(rl_instream), &fds);    
    
        int r = select (fileno(rl_instream)+1, &fds, NULL, NULL, NULL);
        if (r < 0)
        {
            rl_callback_handler_remove ();
            return NULL;
        }
        else if (FD_ISSET (fileno (rl_instream), &fds))
        {
            rl_callback_read_char();
        }
    }

    //rl_callback_handler_remove ();*/

    if(!vty->echo)
    {
        fprintf(vty->data->desc.io_pair[OUT], "%s", vty->prompt);
        fflush(vty->data->desc.io_pair[OUT]);

        char* line = calloc(128, sizeof(char));
        fgets(line, 127, vty->data->desc.io_pair[IN]);
        fprintf(vty->data->desc.io_pair[OUT], "\n");
        fflush(vty->data->desc.io_pair[OUT]);

        return line;
    }
    else if(vty->multi_line)
    {
        int ch = 0;
        memset(vty->buffer, 0, vty->buf_size);
        vty->buf_size = 0;
        while(true)
        {
            // Read multiple lines of input into buffer until \n.\n is found
            ch = fgetc(vty->data->desc.io_pair[IN]);
            if(ch == EOF)
            {
                return NULL;
            }
            
            if(ch == 127)
            {
                if(vty->buf_size > 0)
                {
                    vty->buffer[--vty->buf_size] = 0;
                    fprintf(vty->data->desc.io_pair[OUT], "\b \b");
                    fflush(vty->data->desc.io_pair[OUT]);
                }
                continue;
            }
            if(ch == vty->multiline_stop_char)
            {
                break;
            }
            fprintf(vty->data->desc.io_pair[OUT], "%c", ch);
            fflush(vty->data->desc.io_pair[OUT]);
            if(vty->buf_size + 1 < BUFSIZE)
            {
                vty->buffer[vty->buf_size++] = ch;
            }
        }
        return vty->buffer;
    }
    else
    {
        char* line = readline(vty->prompt);
        if(NULL != line && vty->echo)
        {
            if('\0' != *line && *line != '\n' && vty->type == VTY_STD)
            {
                add_history(line);
            }
        }
        return line;
    }
    //return rl_line_buffer;
}

void    std_show_history(vty_t* vty)
{
    HIST_ENTRY** history_entry = history_list();
    if(NULL != history_entry)
    {
        while(*history_entry)
        {
            vty_write(vty, "%s\n", (*history_entry)->line);
            history_entry++;
        }
    }
}

char*   http_read_cb(vty_t* vty)
{
    int socket = vty->data->desc.socket;
    return http_server_read_request(socket);
}

void    http_write_cb(vty_t* vty, const char* buf, size_t len)
{
    if(vty->buf_size + len < BUFSIZE)
    {
        vty->buf_size += len;
        strncat(vty->buffer, buf, len);
    }
    else
    {
        http_flush_cb(vty);
    }
}

void    http_flush_cb(vty_t* vty)
{
    //if(vty->buf_size > 0)
    {
        int socket = vty->data->desc.socket;
        http_server_write_response(socket, vty->buffer, vty->buf_size);

        vty->buf_size = 0;
        *vty->buffer = 0;
    }
}

