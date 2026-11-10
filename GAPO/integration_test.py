"""Exercise a complete match using the actual server and client programs."""
import os
import subprocess
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

root = Path(__file__).resolve().parent
suffix = '.exe' if os.name == 'nt' else ''

def match(host_move, remote_move, winner):
    server = subprocess.Popen([str(root / ('server' + suffix)), '15482'],
                              stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    client = None
    try:
        while True:
            line = server.stdout.readline()
            if 'listening' in line:
                break
            if not line:
                raise RuntimeError('Server startup failed: ' + server.stderr.read())
        client = subprocess.Popen([str(root / ('client' + suffix)), 'localhost', '15482'],
                                  stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        with ThreadPoolExecutor(max_workers=2) as pool:
            host = pool.submit(server.communicate, input=host_move * 14, timeout=10)
            remote = pool.submit(client.communicate, input=remote_move * 14, timeout=10)
            host_out, host_err = host.result()
            remote_out, remote_err = remote.result()
        assert server.returncode == 0, host_err
        assert client.returncode == 0, remote_err
        assert 'Connected as Player 2.' in remote_out
        for output in (host_out, remote_out):
            assert output.count('--- TURN RESULT ---') == 14, output
            assert f'MATCH WINNER: Player {winner}' in output, output
        assert ('(YOU WIN!)' in host_out) == (winner == 1)
        assert ('(YOU WIN!)' in remote_out) == (winner == 2)
        assert 'Match connection closed.' in remote_out
        print(f'PASS: server Player 1 vs client Player 2, 14 turns, winner Player {winner}')
    finally:
        for process in (client, server):
            if process is not None:
                if process.poll() is None:
                    process.kill()
                process.wait()

if __name__ == '__main__':
    match('1\n', '2\n', 1)
    match('2\n', '1\n', 2)
