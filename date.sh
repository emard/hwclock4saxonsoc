R6=`i2cget -y 0 0x6f 0x06`
R5=`i2cget -y 0 0x6f 0x05`
R4=`i2cget -y 0 0x6f 0x04`
R2=`i2cget -y 0 0x6f 0x02`
R1=`i2cget -y 0 0x6f 0x01`
R0=`i2cget -y 0 0x6f 0x00`

YY="${R6:2:2}"
MON="$((${R5:2:2}-20))"
DD="${R4:2:2}"
HH="${R2:2:2}"
MM="${R1:2:2}"
SS="${R0:2:2}"

echo "20$YY-$MON-$DD $HH:$MM:$SS"
