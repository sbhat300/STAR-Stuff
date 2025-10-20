import os
import select

C_FIFO_LOCATION = '/tmp/cfifo'
PYTHON_FIFO_LOCATION = '/tmp/pyfifo'

READ_SIZE = 2048

buffer = b''
last_check = 0
NULL_TERMINATOR = b'\0'


def do_something(msg: bytes):
    print(f'RECEIVED MESSAGE:\n{msg}')
    
def send_message(msg: bytes):
    written = os.write(py_fifo, msg + NULL_TERMINATOR)
    print(f'{written} bytes sent out of {len(msg) + 1}')

def setup():
    global C_FIFO_LOCATION, PYTHON_FIFO_LOCATION, c_fifo, py_fifo
    print('CREATING FIFOS')
    if not os.path.exists(C_FIFO_LOCATION):
        os.mkfifo(C_FIFO_LOCATION)
    if not os.path.exists(PYTHON_FIFO_LOCATION):
        os.mkfifo(PYTHON_FIFO_LOCATION)

    print('OPENING C FIFO')
    c_fifo = os.open(C_FIFO_LOCATION, os.O_RDONLY | os.O_NONBLOCK)
    print('OPENING PY FIFO')
    py_fifo = os.open(PYTHON_FIFO_LOCATION, os.O_WRONLY) 

def check_for_messages():
    global buffer, last_check, c_fifo, NULL_TERMINATOR
    read, _, _ = select.select([c_fifo], [], [], 0)
    if read:
        data = os.read(c_fifo, READ_SIZE)
        if data:
            buffer += data
            print(f'Message received of length {len(buffer)}')
            null_idx = buffer.find(NULL_TERMINATOR, last_check)
            if null_idx == -1:
                last_check = len(buffer) - 1
            while null_idx != -1:
                message = buffer[:null_idx]
                do_something(message)
                buffer = buffer[null_idx + 1:]
                last_check = 0
                null_idx = buffer.find(NULL_TERMINATOR, last_check)
        else:
            print('Writer closed pipe')
            os.close(c_fifo)
            c_fifo = os.open(C_FIFO_LOCATION, os.O_RDONLY | os.O_NONBLOCK)
            
            
if __name__ == '__main__':
    setup()
    send_message(b'wtfwtfhihihihwafwfawfwfi\0wafwfwaffawef')
    try:
        while True:
            check_for_messages()
    finally:
        print('Closing fifos')
        os.close(py_fifo)
        os.close(c_fifo)
    