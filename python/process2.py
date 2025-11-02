from IPC_helper import setup, send_message, check_for_messages, cleanup

if __name__ == '__main__':
    setup()
    send_message(b'wtfwtfhihihihwafwfawfwfi\0wafwfwaffawef')
    try:
        while True:
            check_for_messages()
    finally:
        cleanup()