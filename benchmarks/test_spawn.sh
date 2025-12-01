#!/bin/bash
set -x

echo "Starting test..."

# Start server
./build/text_mock_server 9888 1000 3 >/dev/null 2>&1 &
SERVER_PID=$!
echo "Server PID: $SERVER_PID"

sleep 2

# Check if server is running
if kill -0 $SERVER_PID 2>/dev/null; then
    echo "Server is running"
else
    echo "Server NOT running"
fi

# Start handler
./build/feed_handler --port 9888 --verbose >/tmp/test_output.txt 2>&1 &
HANDLER_PID=$!
echo "Handler PID: $HANDLER_PID"

# Check if handler is running
sleep 1
if kill -0 $HANDLER_PID 2>/dev/null; then
    echo "Handler is running"
else
    echo "Handler NOT running"
fi

# Wait
sleep 5

# Cleanup
kill $HANDLER_PID 2>/dev/null
kill $SERVER_PID 2>/dev/null
wait

# Show output
echo "=== Output ==="
cat /tmp/test_output.txt | head -20
rm -f /tmp/test_output.txt

echo "Done"
