
with open('scripts/500.txt', 'r') as file:
    fib = file.readlines()

for i in range(len(fib)):
    fib[i] = fib[i].strip().split(' ')

with open('scripts/expected500.txt', 'w') as file:
    for i in range(len(fib)):
        file.write('Writing to /dev/fibonacci, returned the sequence 1\n')
    for i in range(len(fib)):
        file.write('Reading from /dev/fibonacci at offset {}, '\
                   'returned the sequence {}.\n'.format(fib[i][0], fib[i][1]))
    for i in range(len(fib) - 1, -1, -1):
        file.write('Reading from /dev/fibonacci at offset {}, '\
                   'returned the sequence {}.\n'.format(fib[i][0], fib[i][1]))
