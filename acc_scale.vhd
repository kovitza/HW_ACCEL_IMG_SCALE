library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity acc_scale is
    generic (
        G_MAX_ROW_WIDTH : integer := 10;	-- maximum row width = 2^G_MAX_ROW_WIDTH, mamxium allowed value is 32
        G_SCALE_WIDTH   : integer := 3		-- maximum scale = 2^G_SCALE_WIDTH-1, mamxium allowed value is 5
    );
	port (
        reset                  : in  std_logic;                     -- reset
		avs_params_address     : in  std_logic_vector(3 downto 0);  -- params.address
		avs_params_read        : in  std_logic;                     -- .read
		avs_params_readdata    : out std_logic_vector(7 downto 0); 	-- .readdata
		avs_params_write       : in  std_logic;                     -- .write
		avs_params_writedata   : in  std_logic_vector(7 downto 0); 	-- .writedata
		avs_params_waitrequest : out std_logic;                     -- .waitrequest
		clk                    : in  std_logic;                     -- clock
		asi_in_data            : in  std_logic_vector(7 downto 0);  -- in.data
		asi_in_ready           : out std_logic;                     -- .ready
		asi_in_valid           : in  std_logic;                     -- .valid
		asi_in_sop             : in  std_logic;                     -- .startofpacket
		asi_in_eop             : in  std_logic;                     -- .endofpacket
		aso_out_data           : out std_logic_vector(7 downto 0);  -- out.data
		aso_out_ready          : in  std_logic;                     -- .ready
		aso_out_valid          : out std_logic;                     -- .valid
		aso_out_sop            : out std_logic;                     -- .startofpacket
		aso_out_eop            : out std_logic                      -- .endofpacket
	);
end entity acc_scale;

architecture rtl of acc_scale is
        -- GENERAL
    signal bit_busy     : std_logic;
    
    signal bit_reset    : std_logic;
    signal bit_start    : std_logic;
    signal bit_increase : std_logic;
    
    signal int_reset    : std_logic;
	
        -- SCALING AND STREAMING
			-- counters
    signal input_index  : unsigned(G_MAX_ROW_WIDTH-1 downto 0);
    signal output_index : unsigned(G_MAX_ROW_WIDTH-1 downto 0);
    signal pixel_scale  : unsigned(G_SCALE_WIDTH-1 downto 0);
    signal row_scale    : unsigned(G_SCALE_WIDTH-1 downto 0);
	
    signal rows_left    : unsigned(31 downto 0);
	
			-- counter control signals
    signal input_index_decrease     : std_logic;
    signal output_index_decrease    : std_logic;
    signal pixel_scale_decrease     : std_logic;
    signal row_scale_decrease       : std_logic;
    signal rows_left_decrease       : std_logic;
    
    signal input_index_load         : std_logic;
    signal output_index_load        : std_logic;
    signal pixel_scale_load         : std_logic;
    signal row_scale_load           : std_logic;
    signal rows_left_load           : std_logic;
    
			-- other
    signal reg_width    : std_logic_vector(31 downto 0);
    signal reg_height   : std_logic_vector(31 downto 0);
    signal scale        : std_logic_vector(G_SCALE_WIDTH-1 downto 0);
    
			-- ram
	type Mem_t is array (0 to 2**G_MAX_ROW_WIDTH-1) of std_logic_vector(7 downto 0);
	signal memory_ram : Mem_t;	--pravimo ram
	
			-- ram control signals
    signal ram_wr : std_logic;	--postavim ness na ulaz kad je ovo 1 upisuj u ram
    
			-- streaming
    signal int_asi_in_ready  : std_logic;	--postavljeni kao interni da bi proveravali izlaz
    signal int_aso_out_valid : std_logic;	--jer vhdl ne mozes da proveravas izlazni signal pa mora interni
    
    type State_t is (st_reset, st_buffer_not_full, st_buffer_full, st_buffer_rewrite);
    signal reg_current_state, next_state : State_t;
    
        -- AVALON INTERFACE
			-- constants 
			-- address
		constant C_ADDR_WIDTH_0   : std_logic_vector(3 downto 0) := x"0";
		constant C_ADDR_WIDTH_1   : std_logic_vector(3 downto 0) := x"1";
		constant C_ADDR_WIDTH_2   : std_logic_vector(3 downto 0) := x"2";
		constant C_ADDR_WIDTH_3   : std_logic_vector(3 downto 0) := x"3";
		constant C_ADDR_HEIGHT_0  : std_logic_vector(3 downto 0) := x"4";
		constant C_ADDR_HEIGHT_1  : std_logic_vector(3 downto 0) := x"5";
		constant C_ADDR_HEIGHT_2  : std_logic_vector(3 downto 0) := x"6";
		constant C_ADDR_HEIGHT_3  : std_logic_vector(3 downto 0) := x"7";
		constant C_ADDR_STATUS    : std_logic_vector(3 downto 0) := x"8";
		constant C_ADDR_CONTROL   : std_logic_vector(3 downto 0) := x"9";
		--imamo adrese od 0-9 do 10 je reserved
	
			-- signals
			-- strobe			POSTAVIMO ADRESU I WRITE, AKO JE moja adresa i write, onda se generise strobe
	signal strobe_width_0	: std_logic;
	signal strobe_width_1	: std_logic;
	signal strobe_width_2	: std_logic;
	signal strobe_width_3	: std_logic;
	signal strobe_height_0	: std_logic;
	signal strobe_height_1	: std_logic;
	signal strobe_height_2	: std_logic;
	signal strobe_height_3	: std_logic;
	signal strobe_status	: std_logic;
	signal strobe_control	: std_logic;
    
            -- params registers	REGISTRI KOJE KORISTIMO
    signal reg_width_0  	: std_logic_vector(7 downto 0);
	signal reg_width_1  	: std_logic_vector(7 downto 0);
    signal reg_width_2  	: std_logic_vector(7 downto 0);
	signal reg_width_3  	: std_logic_vector(7 downto 0);
    signal reg_height_0 	: std_logic_vector(7 downto 0);
	signal reg_height_1 	: std_logic_vector(7 downto 0);
	signal reg_height_2 	: std_logic_vector(7 downto 0);
	signal reg_height_3 	: std_logic_vector(7 downto 0);
	signal status       	: std_logic_vector(7 downto 0);	
	signal reg_control_autoreset 	: std_logic_vector(7 downto 6);	--GORNJA TRI BITA SU AUTORESET reserved
	signal reg_control_no_autoreset : std_logic_vector(5 downto 0);
	
			-- other
	signal read_out_mux 	: std_logic_vector(7 downto 0);
	signal reg_control  	: std_logic_vector(7 downto 0);
begin
---------------------------------------------------------------------------
-- AVALON INTERFACE	MI SMO AVALON KORISTILI ANALOGNO, ZATO ANALOGNO GENERISEMO SIGNALE RAZLIKA JE U TOME STO IMAMO 10 ADResa
---------------------------------------------------------------------------
	-- strobe
	strobe_width_0 	<= '1' when (avs_params_write = '1') and (avs_params_address = C_ADDR_WIDTH_0) 	else '0';
   strobe_width_1 	<= '1' when (avs_params_write = '1') and (avs_params_address = C_ADDR_WIDTH_1) 	else '0';
	strobe_width_2 	<= '1' when (avs_params_write = '1') and (avs_params_address = C_ADDR_WIDTH_2) 	else '0';
	strobe_width_3 	<= '1' when (avs_params_write = '1') and (avs_params_address = C_ADDR_WIDTH_3) 	else '0';
	strobe_height_0 <= '1' when (avs_params_write = '1') and (avs_params_address = C_ADDR_HEIGHT_0) else '0';
	strobe_height_1 <= '1' when (avs_params_write = '1') and (avs_params_address = C_ADDR_HEIGHT_1) else '0';
	strobe_height_2 <= '1' when (avs_params_write = '1') and (avs_params_address = C_ADDR_HEIGHT_2) else '0';
	strobe_height_3 <= '1' when (avs_params_write = '1') and (avs_params_address = C_ADDR_HEIGHT_3) else '0';
	strobe_status 	<= '1' when (avs_params_write = '1') and (avs_params_address = C_ADDR_STATUS) 	else '0';
	strobe_control 	<= '1' when (avs_params_write = '1') and (avs_params_address = C_ADDR_CONTROL)	else '0';
	
	-- read_out_mux
	read_out_mux <= reg_width_0 	when (avs_params_address = C_ADDR_WIDTH_0) 	else
					reg_width_1 	when (avs_params_address = C_ADDR_WIDTH_1) 	else
					reg_width_2 	when (avs_params_address = C_ADDR_WIDTH_2) 	else
					reg_width_3 	when (avs_params_address = C_ADDR_WIDTH_3) 	else
					reg_height_0 	when (avs_params_address = C_ADDR_HEIGHT_0) else
					reg_height_1 	when (avs_params_address = C_ADDR_HEIGHT_1) else
					reg_height_2 	when (avs_params_address = C_ADDR_HEIGHT_2) else
					reg_height_3 	when (avs_params_address = C_ADDR_HEIGHT_3) else
					status 			when (avs_params_address = C_ADDR_STATUS) 	else
					reg_control 	when (avs_params_address = C_ADDR_CONTROL) 	else
					x"00";
	
	-- reg width 0
    PROC_REG_WIDTH_0: process (clk, int_reset) is
    begin
        if (int_reset = '1') then
            reg_width_0 <= (others => '0');
        elsif (rising_edge(clk)) then
            if (strobe_width_0 = '1') then
                reg_width_0 <= avs_params_writedata;
            end if;
        end if;
    end process PROC_REG_WIDTH_0;
	
	-- reg width 1
	PROC_REG_WIDTH_1: process (clk, int_reset) is
    begin
        if (int_reset = '1') then
            reg_width_1 <= (others => '0');
        elsif (rising_edge(clk)) then
            if (strobe_width_1 = '1') then
                reg_width_1 <= avs_params_writedata;
            end if;
        end if;
    end process PROC_REG_WIDTH_1;
	
	-- reg width 2
	PROC_REG_WIDTH_2: process (clk, int_reset) is
    begin
        if (int_reset = '1') then
            reg_width_2 <= (others => '0');
        elsif (rising_edge(clk)) then
            if (strobe_width_2 = '1') then
                reg_width_2 <= avs_params_writedata;
            end if;
        end if;
    end process PROC_REG_WIDTH_2;
	
	-- reg width 3
	PROC_REG_WIDTH_3: process (clk, int_reset) is
    begin
        if (int_reset = '1') then
            reg_width_3 <= (others => '0');
        elsif (rising_edge(clk)) then
            if (strobe_width_3 = '1') then
                reg_width_3 <= avs_params_writedata;
            end if;
        end if;
    end process PROC_REG_WIDTH_3;
	
	-- reg height 0
	PROC_REG_HEIGHT_0: process (clk, int_reset) is
    begin
        if (int_reset = '1') then
            reg_height_0 <= (others => '0');
        elsif (rising_edge(clk)) then
            if (strobe_height_0 = '1') then
                reg_height_0 <= avs_params_writedata;
            end if;
        end if;
    end process PROC_REG_HEIGHT_0;
	
	-- reg height 1
	PROC_REG_HEIGHT_1: process (clk, int_reset) is
    begin
        if (int_reset = '1') then
            reg_height_1 <= (others => '0');
        elsif (rising_edge(clk)) then
            if (strobe_height_1 = '1') then
                reg_height_1 <= avs_params_writedata;
            end if;
        end if;
    end process PROC_REG_HEIGHT_1;
	
	-- reg height 2
	PROC_REG_HEIGHT_2: process (clk, int_reset) is
    begin
        if (int_reset = '1') then
            reg_height_2 <= (others => '0');
        elsif (rising_edge(clk)) then
            if (strobe_height_2 = '1') then
                reg_height_2 <= avs_params_writedata;
            end if;
        end if;
    end process PROC_REG_HEIGHT_2;
	
	-- reg height 3
	PROC_REG_HEIGHT_3: process (clk, int_reset) is
    begin
        if (int_reset = '1') then
            reg_height_3 <= (others => '0');
        elsif (rising_edge(clk)) then
            if (strobe_height_3 = '1') then
                reg_height_3 <= avs_params_writedata;
            end if;
        end if;
    end process PROC_REG_HEIGHT_3;
	
    -- status
    status(7 downto 1) <= (others => '0');
    status(0) <= bit_busy;
	bit_busy <= '0' when (reg_current_state = st_reset) else '1';
	
	-- reg control autoreset (upper 2 bits)
	PROC_REG_CONTROL_AUTORESET: process (clk, reset) is
    begin
        if (reset = '1') then
            reg_control_autoreset <= (others => '0');
        elsif (rising_edge(clk)) then
            if (strobe_control = '1') then
                reg_control_autoreset <= avs_params_writedata(7 downto 6);
            else
				-- autoreset bits (bit_reset and bit_start)
				reg_control_autoreset <= (others => '0');
			end if;
        end if;
    end process PROC_REG_CONTROL_AUTORESET;
	
	-- reg control no autoreset (bottom 6 bits)
	PROC_REG_CONTROL_NO_AUTORESET: process (clk, int_reset) is
    begin
        if (int_reset = '1') then
            reg_control_no_autoreset <= (others => '0');
        elsif (rising_edge(clk)) then
            if (strobe_control = '1') then
                reg_control_no_autoreset <= avs_params_writedata(5 downto 0);
			end if;
        end if;
    end process PROC_REG_CONTROL_NO_AUTORESET;

	reg_control <= reg_control_autoreset & reg_control_no_autoreset;

	-- reg readdata
	PROC_REG_READDATA: process (clk, int_reset) is
	begin
		if (int_reset = '1') then
			avs_params_readdata <= (others => '0');
		elsif (rising_edge(clk)) then
			avs_params_readdata <= read_out_mux;
		end if;
	end process PROC_REG_READDATA;
	
    -- avs_params_read is unused
    avs_params_waitrequest <= '0';

---------------------------------------------------------------------------
-- GENERAL
---------------------------------------------------------------------------

	-- alias for important bits in control register
    bit_reset       <= reg_control(7);
    bit_start       <= reg_control(6);
    bit_increase    <= reg_control(5);
    scale 			<= reg_control(G_SCALE_WIDTH-1 downto 0);
	
	-- internal reset that allowes software reset by writing to control register
    int_reset <= reset or bit_reset;
	
	-- agregate signals for width and height
	reg_width 	<= reg_width_3 & reg_width_2 & reg_width_1 & reg_width_0;
	reg_height 	<= reg_height_3 & reg_height_2 & reg_height_1 & reg_height_0;
	
---------------------------------------------------------------------------
-- SCALING AND STREAMING
---------------------------------------------------------------------------

-- counters
    -- input index counter
    PROC_CNT_INPUT_INDEX: process (clk, int_reset) is
    begin
        if (int_reset = '1') then
            input_index <= (others => '0');
        elsif (rising_edge(clk)) then
            if (input_index_load = '1') then
                input_index <= unsigned(reg_width(G_MAX_ROW_WIDTH-1 downto 0)) - 1;
            elsif (input_index_decrease = '1') then
                if (input_index = 0) then
                    input_index <= unsigned(reg_width(G_MAX_ROW_WIDTH-1 downto 0)) - 1;
                else
                    input_index <= input_index - 1;
                end if;
            end if;
        end if;
    end process PROC_CNT_INPUT_INDEX;
	
	-- output index counter
    PROC_CNT_OUTPUT_INDEX: process (clk, int_reset) is
    begin
        if (int_reset = '1') then
            output_index <= (others => '0');
        elsif (rising_edge(clk)) then
            if (output_index_load = '1') then
                output_index <= unsigned(reg_width(G_MAX_ROW_WIDTH-1 downto 0)) - 1;
            elsif (output_index_decrease = '1') then
                if (output_index = 0) then
                    output_index <= unsigned(reg_width(G_MAX_ROW_WIDTH-1 downto 0)) - 1;
                else
                    output_index <= output_index - 1;
                end if;
            end if;
        end if;
    end process PROC_CNT_OUTPUT_INDEX;
	
	-- pixel scale counter
    PROC_CNT_PIXEL_SCALE: process (clk, int_reset) is
    begin
        if (int_reset = '1') then
            pixel_scale <= (others => '0');
        elsif (rising_edge(clk)) then
            if (pixel_scale_load = '1') then
                pixel_scale <= unsigned(scale) - 1;
            elsif (pixel_scale_decrease = '1') then
                if (pixel_scale = 0) then
                    pixel_scale <= unsigned(scale) - 1;
                else
                    pixel_scale <= pixel_scale - 1;
                end if;
            end if;
        end if;
    end process PROC_CNT_PIXEL_SCALE;
	
	-- row scale counter
    PROC_CNT_ROW_SCALE: process (clk, int_reset) is
    begin
        if (int_reset = '1') then
            row_scale <= (others => '0');
        elsif (rising_edge(clk)) then
            if (row_scale_load = '1') then
                row_scale <= unsigned(scale) - 1;
            elsif (row_scale_decrease = '1') then
                if (row_scale = 0) then
                    row_scale <= unsigned(scale) - 1;
                else
                    row_scale <= row_scale - 1;
                end if;
            end if;
        end if;
    end process PROC_CNT_ROW_SCALE;
	
	-- rows left counter
    PROC_CNT_ROWS_LEFT: process (clk, int_reset) is
    begin
        if (int_reset = '1') then
            rows_left <= (others => '0');
        elsif (rising_edge(clk)) then
            if (rows_left_load = '1') then
                rows_left <= unsigned(reg_height) - 1;
            elsif (rows_left_decrease = '1') then
                if (rows_left = 0) then
                    rows_left <= unsigned(reg_height) - 1;
                else
                    rows_left <= rows_left - 1;
                end if;
            end if;
        end if;
    end process PROC_CNT_ROWS_LEFT;

    LOGIC_COUNTER_CONTROL: process (reg_current_state, bit_start, bit_increase, asi_in_valid, int_asi_in_ready, int_aso_out_valid, aso_out_ready, input_index, output_index, pixel_scale, row_scale, scale) is
    begin 
    	input_index_decrease    <= '0';
        output_index_decrease   <= '0';
        pixel_scale_decrease    <= '0';
        row_scale_decrease      <= '0';
        rows_left_decrease      <= '0';
        input_index_load        <= '0';
        output_index_load       <= '0';
        pixel_scale_load        <= '0';
        row_scale_load          <= '0';
        rows_left_load          <= '0';

        if ( reg_current_state = st_reset ) then
			-- FSM is in reset state
            if ( bit_start = '1' ) then
				-- FSM will be in running state on next clk 
				-- initialize counters by loading them with data
            	input_index_load    <= '1';
                output_index_load   <= '1';
                pixel_scale_load    <= '1';
                row_scale_load      <= '1';
                rows_left_load      <= '1';
            end if;
        else
            -- sink side
            if ((asi_in_valid = '1') and (int_asi_in_ready = '1')) then
            	-- input transfer occured / pixel was received
                input_index_decrease <= '1';
            end if;

            -- source side
            if (bit_increase = '0') then
                -- decrease
                if ((row_scale = unsigned(scale)-1) and (pixel_scale = unsigned(scale)-1)) then
                    -- this pixel needs to be sent
                    if (aso_out_ready = '1' and int_aso_out_valid = '1') then
                        -- pixel was sent
                        pixel_scale_decrease <= '1';
                        output_index_decrease <= '1';
                        if (output_index = 0) then
                            -- last pixel in row was sent
                            row_scale_decrease <= '1';
                            pixel_scale_load <= '1';
                            rows_left_decrease <= '1';
                        end if;
                    end if;
                elsif ((reg_current_state = st_buffer_rewrite) or (output_index > input_index)) then
                    -- this pixel does not need to be sent => go to next one
                    pixel_scale_decrease <= '1';
                    output_index_decrease <= '1';
                    if (output_index = 0) then
                    	-- last pixel in row was processed
                        row_scale_decrease <= '1';
                        pixel_scale_load <= '1';
                        rows_left_decrease <= '1';
                    end if;
                end if;
            else
                -- increase
                if ((aso_out_ready = '1') and (int_aso_out_valid = '1')) then
                    -- output transfer occured / pixel was sent
                    pixel_scale_decrease <= '1';
                    if (pixel_scale = 0) then
                        -- "scale" number of copies of current pixel were sent
                        output_index_decrease <= '1';
                        if (output_index = 0) then
                            -- last pixel in row was sent
                            row_scale_decrease <= '1';
                            if (row_scale = 0) then
                            	-- "scale" number of copies of current row were sent
                                rows_left_decrease <= '1';
                            end if;
                        end if;
                    end if;
                end if; 
            end if;
        end if;
    end process LOGIC_COUNTER_CONTROL;
                     
-- ram
	-- write ram memory process
	PROC_RAM: process (clk) is
	begin
		if (rising_edge(clk)) then
			if (ram_wr = '1') then
				memory_ram(to_integer(input_index)) <= asi_in_data;
			end if;
		end if;
	end process PROC_RAM;

	-- read ram memory process
	aso_out_data <= memory_ram(to_integer(unsigned(output_index)));

	-- ram write control signal
    ram_wr <= '1' when ((int_asi_in_ready = '1') and (asi_in_valid = '1')) else '0';

-- streaming                     
    
    PROC_REG_CURRENT_STATE: process (clk, int_reset) is
    begin
        if (int_reset = '1') then
            reg_current_state <= st_reset;
        elsif (rising_edge(clk)) then
            reg_current_state <= next_state;
        end if;
    end process PROC_REG_CURRENT_STATE;
    
    LOGIC_STREAMING_PROTOCOL: process (reg_current_state, bit_start, bit_increase, asi_in_valid, aso_out_ready, input_index, output_index, pixel_scale, row_scale, rows_left, scale) is
    begin
        case (reg_current_state) is
            when st_reset =>    
                next_state <= st_reset;
                
                -- sink side
                int_asi_in_ready <= '0';
                
                --source side
                int_aso_out_valid <= '0';
                
                if (bit_start = '1') then
					-- FSM will be in running state on next clk 
                    next_state <= st_buffer_not_full;
                end if;
            when st_buffer_not_full =>
                next_state <= st_buffer_not_full;
                
                -- sink side
                int_asi_in_ready <= '1';
                if ((asi_in_valid = '1') and (input_index = 0)) then
                    -- last pixel in current row was received
                    if ((bit_increase = '0') or (row_scale = 0)) then
                    	-- decrese or scale = 1
                        next_state <= st_buffer_rewrite;
                    else
                    	-- increse and scale > 1
                        next_state <= st_buffer_full;
                    end if;
                end if;
                
                -- source side
                int_aso_out_valid <= '0';
                if (bit_increase = '0') then
                    -- decrease
                    if ((output_index > input_index) and ((row_scale = unsigned(scale)-1) and (pixel_scale = unsigned(scale)-1))) then
                    	-- if unprocessed pixel is available and needs to be sent
                        int_aso_out_valid <= '1';
                    end if;
                else
                    -- increase
                    if (output_index > input_index) then
                        -- if any unprocessed pixel is available
                        int_aso_out_valid <= '1';
                    end if;
                end if;
            when st_buffer_full =>
                next_state <= st_buffer_full;
                
                -- sink side
                int_asi_in_ready <= '0';
                
                -- source side
                int_aso_out_valid <= '1';
                if ((aso_out_ready = '1') and (pixel_scale = 0) and (output_index = 0) and (row_scale = 1)) then
                    -- last pixel in row was sent and row_scale = 1
                    next_state <= st_buffer_rewrite;
                end if;
            when st_buffer_rewrite =>
            next_state <= st_buffer_rewrite;
            
            -- sink side
            int_asi_in_ready <= '0';
            if ((input_index > output_index) and (rows_left /= 0)) then
                -- space for new pixel is available and current row is not last
                int_asi_in_ready <= '1';
            end if;
            
            -- source side
            int_aso_out_valid <= '0';
            if (bit_increase = '0') then
            	-- decrease
                if ((row_scale = unsigned(scale)-1) and (pixel_scale = unsigned(scale)-1)) then
                     -- this pixel needs to be sent
                    int_aso_out_valid <= '1';
                    if ((aso_out_ready = '1') and (output_index = 0)) then
                    	-- last pixel in row was sent
                        if (rows_left = 0) then
                            -- this was last row
                            next_state <= st_reset;
                        else
                            -- this was not last row
                            next_state <= st_buffer_not_full;
                        end if;
                    end if;
                elsif (output_index = 0) then
                    -- this pixel does not need to be sent and last pixel in row was processed
                    if (rows_left = 0) then
                        -- this was last row
                        next_state <= st_reset;
                    else
                        -- this was not last row
                        next_state <= st_buffer_not_full;
                    end if;
                end if;
            else
            	-- increase
                int_aso_out_valid <= '1';
                if ((aso_out_ready = '1') and (pixel_scale = 0) and (output_index = 0)) then
                    -- last pixel in row was sent
                    if (rows_left = 0) then
                        -- this was last row
                        next_state <= st_reset;
                    else
                    	-- this was not last row
                        next_state <= st_buffer_not_full;
                    end if;
                end if;
            end if;
        end case;
    end process LOGIC_STREAMING_PROTOCOL;
    
    -- asi_in_sop is unused
    -- asi_in_eop is unused
    aso_out_sop 	<= '0';
    aso_out_eop 	<= '0';
    asi_in_ready 	<= int_asi_in_ready;
    aso_out_valid 	<= int_aso_out_valid;   
    
end architecture rtl; -- of acc_scale
