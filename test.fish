#!/usr/bin/env fish

set target_url "https://arnavrawat.xyz"
set num_requests 200
set temp_file (mktemp)

echo "Starting $num_requests parallel requests to $target_url..."
echo "Recording response times. Please wait..."

# Loop 100 times
for i in (seq $num_requests)
    # -w "%{time_total}\n": extracts the total transaction time in seconds
    # >> $temp_file: appends the time to our temporary file
    curl -s -o /dev/null -w "%{time_total}\n" $target_url >> $temp_file &
end

# Pause the script until all background curl jobs finish
wait

# Calculate the average time using awk
set average_time (awk '{ sum += $1 } END { if (NR > 0) print sum / NR }' $temp_file)

echo "-----------------------------------"
echo "All $num_requests requests finished."
echo "Average response time: $average_time seconds."
echo "-----------------------------------"

# Clean up the temporary file
rm $temp_file
