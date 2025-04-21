#!/bin/bash

# List of options for mgm
options=("rocksdb")
parallel_mode=("off")

INPUT_FILE="/home/marvin/data/last_iteration.dd"
OUTPUT_FILE="/home/marvin/master-thesis-code/mgm-on-disc/solutions/mgm-debug-out-big"
LOG_FILE="/home/marvin/master-thesis-code/mgm-on-disc/logs.txt"

for threshold in {180..180..+10}; do
    input_file="/mnt/d/test_data/${threshold}.dd"
    output_file="/mnt/c/MgmExperiments/results/seqseq-parallel"

    # input_file_no="/mnt/d/test_data/40.dd"

    # echo "Running mgm with option: no save mode and threshold $threshold"
    #     /home/marvin/master-thesis-code/mgm-on-disc/builddir_release/mgm \
    #         -i "$input_file_no" \
    #         -o "$output_file" \
    #         --mode seqseq

    # mv "$LOG_FILE" "/mnt/c/MgmExperiments/Mgm-logs/seqseq/40-no.txt"
    for mode in "${parallel_mode[@]}"; do
        for option in "${options[@]}"; do
            echo "Running mgm with option: $option and threshold $threshold"
            /home/marvin/master-thesis-code/mgm-on-disc/builddir_release/mgm \
                -i "$input_file" \
                -o "$output_file" \
                --mode seqseq \
                --save-mode "$option" \
                --parallel-cache-mode "$mode"
            if [ $? -ne 0 ]; then
                echo "Error: mgm failed to execute."
                exit 1
            fi
            mv "$LOG_FILE" "/mnt/c/MgmExperiments/Mgm-logs/load/${threshold}-$option-parallel-$mode-new.txt"
        done
    done
done

# for threshold in {40..50..+10}; do
#     input_file="/mnt/d/test_data/${threshold}.dd"
#     output_file="/mnt/c/MgmExperiments/results/seqseq-parallel"
#     for mode in "${parallel_mode[@]}"; do
#         for option in "${options[@]}"; do
#             echo "Running mgm with option: $option and threshold $threshold"
#             /home/marvin/master-thesis-code/mgm-on-disc/builddir_release/mgm \
#                 -i "$input_file" \
#                 -o "$output_file" \
#                 --mode seqseq \
#                 --save-mode "$option" \
#                 --parallel-cache-mode "$mode"
#             if [ $? -ne 0 ]; then
#                 echo "Error: mgm failed to execute."
#                 exit 1
#             fi
#             mv "$LOG_FILE" "/mnt/c/MgmExperiments/Mgm-logs/seqseq-parallel6/${threshold}-$option-parallel-$mode.txt"
#         done
#     done
# done


exit 0