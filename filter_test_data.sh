#!/bin/bash

# Input file and output file
input_file="/home/marvin/data/last_iteration.dd"


echo "start"

for threshold in {80..10..-10}; do
    output_file=/mnt/d/test_data/${threshold}.dd

    echo "Filtering for threshold $threshold started."

    awk -v outfile="$output_file" -v th="$threshold" '
    /^gm/ && $2 < th && $3 < th { print_flag=1 } 
    /^gm/ && ($2 >= th || $3 >= th) { print_flag=0 } 
    print_flag { print > outfile }' "$input_file"

    echo "Filtering complete for threshold $threshold. Output saved to $output_file."
done
echo "Task completed"