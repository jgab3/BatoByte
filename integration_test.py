"""Integration smoke test with two automated socket clients."""
import subprocess, socket, threading, time

port=15482
server=subprocess.Popen(['./server',str(port)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
errors=[]; states={}

def run_client(i):
    try:
        sock=socket.create_connection(('127.0.0.1',port),timeout=5)
        sock.settimeout(10)
        r=sock.makefile('r')
        results=[]
        while True:
            msg=r.readline()
            if not msg: break
            msg=msg.strip()
            if msg.startswith('WELCOME|'): continue
            if msg=='LOADOUT': sock.sendall((('7' if i==0 else '56')+'\n').encode())
            elif msg.startswith('POWER|'):
                mask=int(msg.split('|')[1]);chosen=next(j+1 for j in range(7) if mask & (1<<j))
                sock.sendall((str(chosen)+'\n').encode())
            elif msg.startswith('TURN|'):
                sock.sendall((('1 0' if i==0 else '2 0')+'\n').encode())
            elif msg.startswith('RESULT|'): results.append(msg)
            elif msg.startswith('ERROR|'): raise RuntimeError(msg)
            elif msg=='CLOSE': break
            else: raise RuntimeError('unexpected '+msg)
        states[i]=results
        sock.close()
    except Exception as exc: errors.append((i,str(exc)))
threads=[]
try:
    time.sleep(.2)
    for i in range(2):
        t=threading.Thread(target=run_client,args=(i,),daemon=True)
        t.start();threads.append(t)
    for t in threads:t.join(12)
    if any(t.is_alive() for t in threads):raise RuntimeError('clients timeout')
    if errors:raise RuntimeError(str(errors))
    assert len(states[0])==len(states[1])==14,(len(states.get(0,[])),len(states.get(1,[])))
    last=states[0][-1].split('|')
    expected_winner = 0 if last[5] == '0' else 1
    assert int(last[-2])==expected_winner,states[0][-1]
    assert states[0][-1]==states[1][-1]
    print(f'PASS: two real localhost sockets, 2 rounds, 14 turns, match winner Player {expected_winner+1}')
finally:
    try:server.wait(timeout=3)
    except subprocess.TimeoutExpired:server.kill()
    print('Server output:',server.stdout.read().strip())
    err=server.stderr.read().strip()
    if err:print('Server errors:',err)
