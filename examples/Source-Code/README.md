# Project Group 3

## Authors:

- Nguyen Quy Duc (A0244126M)
- Ong Zheng Long (A0233164M)

## Source Code

- `defs_and_types.c`: Defines and types for task 1
- `task_1_a_group_3.c`: Task 1c for node A
- `task_1_b_group_3.c`: Task 1c for node B
- `defs_and_types_2.c`: Defines and types for task 2
- `task_2_a_group_3.c`: Collect data and transmitter node for task 2
- `task_2_b_group_3.c`: Receiver node for task 2
- `task_2_bonus_a_group_3.c`: Collect data and transmitter node for bonus task
- `task_2_bonus_b_group_3.c`: Receiver node for bonus task
- `Makefile`: make file

## How to run the code

1. Compile the code using:

```
make TARGET=cc26x0-cc13x0 BOARD=sensortag/cc2650
```

2. Flash the node a and node b executable files to the corresponding different sensors using Uniflash. Each task has 2 different executable files for 2 different nodes.
3. Open the terminal and start the sensors.
