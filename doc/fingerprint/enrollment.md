# UART Finger Print Enrollment

```plantuml
@startuml
start

* Send CMD=0x01 command
if (database is full ?) then (yes)
    : Response Q3=ACK_FULL
else (no)
    : Acquire fingerprint
    if (timeout?) then (yes)
        : Response Q3=ACK_TIMEOUT
    else (no)
        : Process image
        if (eigenvalue is less ?) then (yes)
            : Response Q3=ACK_FAIL
        else (no)
            : Response Q3=ACK_SUCCESS
            : Send CMD=0x02 command
            : Acquire fingerprint
            if (timeout?) then (yes)
                : Response Q3=ACK_TIMEOUT
            else (no)
                : Process image
                if (eigenvalue is less ?) then (yes)
                    : Response Q3=ACK_FAIL
                else (no)
                    : Response Q3=ACK_SUCCESS
                    : Send CMD=0x03 command
                    : Acquire fingerprint
                    if (timeout?) then (yes)
                        : Response Q3=ACK_TIMEOUT
                    else (no)
                        : Process image
                        if (eigenvalue is less ?) then (yes)
                            : Response Q3=ACK_FAIL
                        else (no)
                            if (Is unique?) then (yes)
                                : Add fingerprint to database
                                : Response Q3=ACK_SUCCESS
                            else (no)
                                : Response Q3=ACK_User_EXIST
                            endif
                        endif
                    endif
                endif
            endif
        endif
    endif
endif
end
    

@enduml
```