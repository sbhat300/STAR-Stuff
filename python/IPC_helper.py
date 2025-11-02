
import os
import select

C_FIFO_LOCATION = '/tmp/cfifo'
PYTHON_FIFO_LOCATION = '/tmp/pyfifo'

READ_SIZE = 2048

buffer = b''
message_len = 0 
message = b'' 


def do_something(length: int, msg: bytes):
    print(f'RECEIVED MESSAGE WITH LENGTH {length}:\n{msg}')
    
def send_message(msg: bytes):
    if(len(msg) > 255):
        print("Message length is greater than 255")
        return
    message_len = bytes([len(msg)])
    written = os.write(py_fifo, message_len + msg)
    print(f'{written} bytes sent out of {len(msg) + 1} (+1)')

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
    """
    Protocol: [1-byte payload length][payload]
    """
    global buffer, c_fifo, message_len, message
    
    read_ready, _, _ = select.select([c_fifo], [], [], 0)
    
    if not read_ready:
        return 

    try:
        data = os.read(c_fifo, READ_SIZE)
    except OSError as e:
        print(f"Pipe read error: {e}")
        return

    if not data:
        print('Writer closed pipe, reopening...')
        os.close(c_fifo)
        c_fifo = os.open(C_FIFO_LOCATION, os.O_RDONLY | os.O_NONBLOCK)
        buffer = b''
        message = b''
        message_len = 0
        return

    buffer += data
    print(f'Message in buffer of length {len(data)}')

    while True:
        if message_len == 0:
            if len(buffer) < 1:
                break 
            
            message_len = int(buffer[0])
            buffer = buffer[1:] 
            message = b''     
            
        bytes_needed = message_len - len(message)
        bytes_available = len(buffer)
        
        if bytes_available >= bytes_needed:
            message += buffer[:bytes_needed]
            do_something(len(message), message)
            buffer = buffer[bytes_needed:]
            message_len = 0
            message = b''
        else:
            message += buffer
            buffer = b''
            break
            
def cleanup():
    print('Closing fifos')
    os.close(py_fifo)
    os.close(c_fifo)