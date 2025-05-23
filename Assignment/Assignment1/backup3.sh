#!/bin/bash

# Check if the user has entered enough variables
# data file name at least input 1 variable
# after input the data file name, "employee" also MUST input to seperate the emplyee data
# which also count as 1 variable
# the third variable is employee data which allow user to input at least one emplyoee ID or name
if [ "$#" -lt 3 ]; then
    echo "Input Format: $0 <HR_file | Sales_file | hkpolyu*> employee 
[employee_ids | employee_names...]"
    exit 1
fi



# get the data file name and save it in a global varialbe
# here i set it before second verable "employee"
# all user input will save in DATA_FILES this array
DATA_FILES=()
for arg in "$@"; do
    if [[ "$arg" == "employee" ]]; then
        break
    fi
    DATA_FILES+=("$arg")
done



# after saving the data in DATA_FILES
# check is it valid input first
# -f function is  checking the file is exist or not in the directory
for DATA_FILE in "${DATA_FILES[@]}"; do
    if [[ ! -f "$DATA_FILE" ]]; then
        echo "Error: Data file '$DATA_FILE' does not exist."
        exit 1
    fi
done



# the second checking for user who MUST input employee ID or name after "employee"
# the $# will count the total file name varialbe user input
# the add 1 mean after data file name, add the "employee"
# if the condition is true, that means user they do not input any data after "employee"
if [[ "$#" -eq "${#DATA_FILES[@]}+1" ]]; then
    echo "No employee IDs or names provided."
    exit 1
fi



# use global variable to save the user input after "employee" as the employee data
EMPLOYEE_INPUTS=("${@:${#DATA_FILES[@]}+1}")



# after get all employee inputs, create two array for checking the input is valid or not
EMPLOYEE_DATA=()
INVALID_INPUTS=()



# use for-loop to check
# if user not input the number, it will jump to else condition.
# here is the first situation that user input employee ID
# if the ID which is exist in employees.dat then save it in EMPLOYEE_IDS array, if not, save it in INVALID_INPUTS array
for EMPLOYEE_INPUT in "${EMPLOYEE_INPUTS[@]}"; do
    if [[ "$EMPLOYEE_INPUT" =~ ^[0-9]+$ ]]; then
        if grep -q "$EMPLOYEE_INPUT" "employees.dat"; then
            EMPLOYEE_DATA+=("$EMPLOYEE_INPUT")
        else
            INVALID_INPUTS+=("$EMPLOYEE_INPUT")
	    break
        fi
    else
	# here is for user who input employee name
	# here will catch the data from employees.dat first
	# then seperate it will two different variable EMPLOYEE_ID and EMPLOYEE_NAME
	# use the EMPLOYEE_NAME to compare with EMPLOYEE_INPUT
        while read -r line; do
            EMPLOYEE_ID=$(echo "$line" | awk '{print $1}')
            EMPLOYEE_NAME=$(echo "$line" | awk '{print $2}')
            if [[ "${EMPLOYEE_NAME,,}" == "${EMPLOYEE_INPUT,,}" ]]; then
                EMPLOYEE_DATA+=("$EMPLOYEE_ID")
	    else
		INVALID_INPUTS+=("$EMPLOYEE_INPUT")
		break		
            fi
        done< <(grep -i "$EMPLOYEE_INPUT" "employees.dat")
    fi
done



# the error message if user input invalid employee ID or name
if [ "${#INVALID_INPUTS[@]}" -gt 0 ]; then
    echo "Invalid employee ID(s) or name(s): ${INVALID_INPUTS[*]}"
    exit 1
fi



# use for-loop to get every employee details from EMPLOYEE_DATA array
# and seperate it between employee ID and name
for EMPLOYEE_ID in "${EMPLOYEE_DATA[@]}"; do
    EMPLOYEE_INFO=$(grep "$EMPLOYEE_ID" "employees.dat")
    EMPLOYEE_NAME=$(echo "$EMPLOYEE_INFO" | awk '{print $2}')
    echo "Attendance Report for $EMPLOYEE_ID $EMPLOYEE_NAME"



    # Initialize the value
    HR_PRESENT=0
    HR_ABSENT=0
    HR_LEAVE=0
    SALES_PRESENT=0
    SALES_ABSENT=0
    SALES_LEAVE=0
    MARKETING_PRESENT=0
    MARKETING_ABSENT=0
    MARKETING_LEAVE=0
    declare -A LATEST_STATUS



    # loop every data file name
    for DATA_FILE in "${DATA_FILES[@]}"; do
        FOUND_ATTENDANCE=0

        # check the file name is exist or not
        if [[ -f "$DATA_FILE" ]]; then

            while read -r attendance; do
                EMP_ID=$(echo "$attendance" | awk '{print $1}')  # print到所有ID list
                STATUS=$(echo "$attendance" | awk '{print $2}')  # print到所有status list
                # update sattus
                LATEST_STATUS["$EMP_ID"]="$STATUS"

                if [[ "$attendance" == *"$EMPLOYEE_ID"* ]]; then
                    FOUND_ATTENDANCE=1  
                fi

            done < "$DATA_FILE"


            if [[ "$FOUND_ATTENDANCE" -eq 1 ]]; then
                if [[ "$DATA_FILE" == *"HR"* && ${LATEST_STATUS["$EMPLOYEE_ID"]} == *"Present"* ]]; then
                    HR_PRESENT=$((HR_PRESENT + 1))
                    echo "Department HR: ${LATEST_STATUS["$EMPLOYEE_ID"]} $HR_PRESENT"
                elif [[ "$DATA_FILE" == *"Sales"* && ${LATEST_STATUS["$EMPLOYEE_ID"]} == *"Present"* ]]; then
                    SALES_PRESENT=$((SALES_PRESENT + 1))
                    echo "Department Sales: ${LATEST_STATUS["$EMPLOYEE_ID"]} $SALES_PRESENT"
                elif [[ "$DATA_FILE" == *"Marketing"* && ${LATEST_STATUS["$EMPLOYEE_ID"]} == *"Present"* ]]; then
                    MARKETING_PRESENT=$((MARKETING_PRESENT + 1))
                    echo "Department Marketing: ${LATEST_STATUS["$EMPLOYEE_ID"]} $MARKETING_PRESENT"
                fi


                if [[ "$DATA_FILE" == *"HR"* && ${LATEST_STATUS["$EMPLOYEE_ID"]} == *"Absent"* ]]; then
                    HR_ABSENT=$((HR_ABSENT + 1))
                    echo "Department HR: ${LATEST_STATUS["$EMPLOYEE_ID"]} $HR_ABSENT"
                elif [[ "$DATA_FILE" == *"Sales"* && ${LATEST_STATUS["$EMPLOYEE_ID"]} == *"Absent"* ]]; then
                    SALES_ABSENT=$((SALES_ABSENT + 1))
                    echo "Department Sales: ${LATEST_STATUS["$EMPLOYEE_ID"]} $SALES_ABSENT"
                elif [[ "$DATA_FILE" == *"Marketing"* && ${LATEST_STATUS["$EMPLOYEE_ID"]} == *"Absent"* ]]; then
                    MARKETING_ABSENT=$((MARKETING_ABSENT + 1))
                    echo "Department Marketing: ${LATEST_STATUS["$EMPLOYEE_ID"]} $MARKETING_ABSENT"
                fi


                if [[ "$DATA_FILE" == *"HR"* && ${LATEST_STATUS["$EMPLOYEE_ID"]} == *"Leave"* ]]; then
                    HR_LEAVE=$((HR_LEAVE + 1))
                    echo "Department HR: ${LATEST_STATUS["$EMPLOYEE_ID"]} $HR_LEAVE"
                elif [[ "$DATA_FILE" == *"Sales"* && ${LATEST_STATUS["$EMPLOYEE_ID"]} == *"Leave"* ]]; then
                    SALES_LEAVE=$((SALES_LEAVE + 1))
                    echo "Department Sales: ${LATEST_STATUS["$EMPLOYEE_ID"]} $SALES_LEAVE"
                elif [[ "$DATA_FILE" == *"Marketing"* && ${LATEST_STATUS["$EMPLOYEE_ID"]} == *"Leave"* ]]; then
                    MARKETING_LEAVE=$((MARKETING_LEAVE + 1))
                    echo "Department Marketing: ${LATEST_STATUS["$EMPLOYEE_ID"]} $MARKETING_LEAVE"
                fi

            else

                if [[ "$DATA_FILE" == *"HR"* ]]; then
                    echo "Department HR: N/A"
                elif [[ "$DATA_FILE" == *"Sales"* ]]; then
                    echo "Department Sales: N/A"
                elif [[ "$DATA_FILE" == *"Marketing"* ]]; then
                    echo "Department Marketing: N/A"                    
                    
                fi
            fi

        fi
    done



    # loop every data file name
    # for DATA_FILE in "${DATA_FILES[@]}"; do
    #     FOUND_ATTENDANCE=0

    #     # check the file name is exist or not
    #     if [[ -f "$DATA_FILE" ]]; then

    #         # use $attendance to save the attendance value with valid employee ID
    #         # since the file have any key word include "HR" or "Sales"
    #         # after confirm the file name
    #         # count the status with match employee ID

    #         while read -r attendance; do
    #             if [[ "$attendance" == *"$EMPLOYEE_ID"* ]]; then

    #                 case $attendance in
    #                     *"Present"*)
    #                         if [[ "$DATA_FILE" == *"HR"* && "${TEMP_STATUS["$EMPLOYEE_ID"]}" == "Present" ]]; then
    #                             HR_PRESENT=$((HR_PRESENT + 1))
    #                             echo "Department HR: Present $HR_PRESENT"
    #                         elif [[ "$DATA_FILE" == *"Sales"* && "${TEMP_STATUS["$EMPLOYEE_ID"]}" == "Present" ]]; then
    #                             SALES_PRESENT=$((SALES_PRESENT + 1))
    #                             echo "Department Sales: Present $SALES_PRESENT"
    #                         elif [[ "$DATA_FILE" == *"Marketing"* && "${TEMP_STATUS["$EMPLOYEE_ID"]}" == "Present" ]]; then
    #                             MARKETING_PRESENT=$((MARKETING_PRESENT + 1))
    #                             echo "Department Marketing: Present $MARKETING_PRESENT"
    #                         fi
    #                     ;;
    #                     *"Absent"*)
    #                         if [[ "$DATA_FILE" == *"HR"* && "${TEMP_STATUS["$EMPLOYEE_ID"]}" == "Absent" ]]; then
    #                             HR_ABSENT=$((HR_ABSENT + 1))
    #                             echo "Department HR: Absent $HR_ABSENT"
    #                         elif [[ "$DATA_FILE" == *"Sales"* && "${TEMP_STATUS["$EMPLOYEE_ID"]}" == "Absent" ]]; then
    #                             SALES_ABSENT=$((SALES_ABSENT + 1))
    #                             echo "Department Sales: Absent $SALES_ABSENT"
    #                         elif [[ "$DATA_FILE" == *"Marketing"* && "${TEMP_STATUS["$EMPLOYEE_ID"]}" == "Absent" ]]; then
    #                             MARKETING_ABSENT=$((MARKETING_ABSENT + 1))
    #                             echo "Department Marketing: Absent $MARKETING_ABSENT"
    #                         fi
    #                     ;;
    #                     *"Leave"*)
    #                         if [[ "$DATA_FILE" == *"HR"* && "${TEMP_STATUS["$EMPLOYEE_ID"]}" == "Leave" ]]; then
    #                             HR_LEAVE=$((HR_LEAVE + 1))
    #                             echo "Department HR: Leave $HR_LEAVE"
    #                         elif [[ "$DATA_FILE" == *"Sales"* && "${TEMP_STATUS["$EMPLOYEE_ID"]}" == "Leave" ]]; then
    #                             SALES_LEAVE=$((SALES_LEAVE + 1))
    #                             echo "Department Sales: Leave $SALES_LEAVE"
    #                         elif [[ "$DATA_FILE" == *"Marketing"* && "${TEMP_STATUS["$EMPLOYEE_ID"]}" == "Leave" ]]; then
    #                             MARKETING_LEAVE=$((MARKETING_LEAVE + 1))
    #                             echo "Department Marketing: Leave $MARKETING_LEAVE"
    #                         fi
    #                     ;;
    #                 esac
    #                 FOUND_ATTENDANCE=1  
    #             fi
    #         done < "$DATA_FILE"  # which mean the $attendance will load the data from $DATA_FILE



    #         # here is check is it the employee ID exist in this data file
    #         # if not will show N/A
    #         if [[ "$FOUND_ATTENDANCE" -eq 0 ]]; then
    #             if [[ "$DATA_FILE" == *"HR"* ]]; then
    #                 echo "Department HR: N/A"
    #             elif [[ "$DATA_FILE" == *"Sales"* ]]; then
    #                 echo "Department Sales: N/A"
    #             elif [[ "$DATA_FILE" == *"Marketing"* ]]; then
    #                 echo "Department Marketing: N/A"                    
    #             fi
    #         fi
	# fi
    # done

    echo ""
    
    # count the total number
    TOTAL_PRESENT=$((HR_PRESENT + SALES_PRESENT + MARKETING_PRESENT))
    TOTAL_ABSENT=$((HR_ABSENT + SALES_ABSENT + MARKETING_ABSENT))
    TOTAL_LEAVE=$((HR_LEAVE + SALES_LEAVE + MARKETING_LEAVE))

    # print the output
    echo "Total Present Days: $TOTAL_PRESENT"
    echo "Total Absent Days: $TOTAL_ABSENT"
    echo "Total Leave Days: $TOTAL_LEAVE"
    echo ""
done

