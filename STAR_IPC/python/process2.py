from IPC_helper import setup, send_message, check_for_messages

def do_something(length: int, msg: bytes):
    print(f'RECEIVED MESSAGE WITH LENGTH {length}:\n{msg}')

if __name__ == '__main__':
    setup()
    send_message(b'wtfwtfhihihihwafwfawfwfi\0wafwfwaffawef')
    while True:
        check_for_messages(do_something)