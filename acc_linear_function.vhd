-- Linear function accelerator

-- This module is used for accelerating linear function Y=AX+B calculations.
-- Parameters A and B are stored in two 8 bit registers which can be accessed
-- from the CPU via Avalon-MM slave interface.
-- 
-- Input and output data are transferred as data streams using two Avalon-ST interfaces.
-- For input Avalon-ST sink interface with 8-bit datapath and readyLatency 0 is used.
-- Output 16-bit wide is transmitted trough Avalon-ST source interface 16-bit wide with readyLatency 0.

--Author: Dragomir El Mezeni
--Institution/Company: University of Belgrade, School of Electrical Engineering
--Created: 25.11.2016.
--Last modified: 29.11.2016. (header comments added)

library IEEE;
use IEEE.std_logic_1164.all;
use IEEE.numeric_std.all;

entity acc_linear_function is
	port (
		reset                  : in  std_logic                     := '0';             --  reset.reset
		avs_params_address     : in  std_logic                     := '0';             -- params.address
		avs_params_read        : in  std_logic                     := '0';             --       .read
		avs_params_readdata    : out std_logic_vector(7 downto 0);                     --       .readdata
		avs_params_write       : in  std_logic                     := '0';             --       .write
		avs_params_writedata   : in  std_logic_vector(7 downto 0)  := (others => '0'); --       .writedata
		avs_params_waitrequest : out std_logic;                                        --       .waitrequest
		clk                    : in  std_logic                     := '0';             --  clock.clk
		asi_in_data            : in  std_logic_vector(7 downto 0)  := (others => '0'); --     in.data
		asi_in_ready           : out std_logic;                                        --       .ready
		asi_in_valid           : in  std_logic                     := '0';             --       .valid
		asi_in_sop             : in  std_logic                     := '0';             --       .startofpacket
		asi_in_eop             : in  std_logic                     := '0';             --       .endofpacket
		aso_out_data           : out std_logic_vector(15 downto 0);                    --    out.data
		aso_out_ready          : in  std_logic                     := '0';             --       .ready
		aso_out_valid          : out std_logic;                                        --       .valid
		aso_out_sop            : out std_logic;                                        --       .startofpacket
		aso_out_eop            : out std_logic;                                        --       .endofpacket
		aso_out_empty          : out std_logic                                         --       .empty
	);
end entity acc_linear_function;

architecture rtl of acc_linear_function is
	signal a_reg : std_logic_vector(7 downto 0);
	signal b_reg : std_logic_vector(7 downto 0);
	
	constant A_ADDR : std_logic := '0';
	constant B_ADDR : std_logic := '1';
	
	signal a_strobe : std_logic;
	signal b_strobe : std_logic;
	
	signal read_out_mux : std_logic_vector(7 downto 0);
	
	type state is (wait_input, -- waiting for input data, no valid data in output register, no valid data in input register
				process_input, -- waiting for input to be processed, valid data in input register, no valid data in output register
				  wait_output, -- waiting for someone to receive data from output register, valid data in output register, no valid data in input register
	  wait_output_and_process, -- waiting for someone to receive data from output register, valid data in output register, valid data in input register
	             full_process); -- in this state in each clock new data is written in input register, and new output data is written in output register
	
	signal current_state, next_state : state;

	signal input_sample : signed(7 downto 0);
	signal output_sample : signed(15 downto 0);
	
	signal int_asi_in_ready : std_logic;


begin

	a_strobe <= '1' when (avs_params_write = '1') and (avs_params_address = A_ADDR) else '0';
	b_strobe <= '1' when (avs_params_write = '1') and (avs_params_address = B_ADDR) else '0';
	
	read_out_mux <= a_reg when (avs_params_address = A_ADDR) else
						 b_reg when (avs_params_address = B_ADDR) else
						 "00000000";
	
	write_reg_a: process(clk, reset)
	begin
		if (reset = '1') then
			a_reg <= "00000010";
		elsif (rising_edge(clk)) then
			if (a_strobe = '1') then
				a_reg <= avs_params_writedata;
			end if;
		end if;
	end process;

	write_reg_b: process(clk, reset)
	begin
		if (reset = '1') then
			b_reg <= "00000011";
		elsif (rising_edge(clk)) then
			if (b_strobe = '1') then
				b_reg <= avs_params_writedata;
			end if;
		end if;
	end process;

	read_regs: process(clk, reset)
	begin
		if (reset = '1') then
			avs_params_readdata <= "00000000";
		elsif (rising_edge(clk)) then
			avs_params_readdata <= read_out_mux;
		end if;
	end process;
	
	process_sample : process(clk, reset)
	begin
		if (reset = '1') then
			output_sample <= x"BEEF";
		elsif (rising_edge(clk)) then
-- New data can be written in output register only when there is no valid data in output register, or the last data is successfully sent to the output.
-- In process_inupt state there is no pending data in output register and it can be safely overwritten.
-- In full_process and wait_output_and_process states there is pending data in output register which can't be overwritten. Activation of ready signal on
-- output streaming interface signals that this data is successfully sent and so the output register can be overwritten with the new data.
-- In wait_input and wait_output states there is no valid data in the input register which should be processed.
			if ((current_state = process_input) or ((current_state = full_process or current_state = wait_output_and_process) and aso_out_ready = '1')) then
				output_sample <= signed(a_reg)*input_sample + signed(b_reg);
			end if;
		end if;
	end process;
	
	aso_out_data <= std_logic_vector(output_sample);

	read_sample : process(clk, reset)
	begin
		if (reset = '1') then
			input_sample <= x"00";
		elsif (rising_edge(clk)) then
			if (int_asi_in_ready = '1' and asi_in_valid = '1') then
				input_sample <= signed(asi_in_data);
			end if;
		end if;
	end process;
	
	control_fsm: process(clk, reset)
	begin
		if (reset = '1') then
			current_state <= wait_input;
		elsif (rising_edge(clk)) then
			current_state <= next_state;
		end if;
	end process;
	
	
	streaming_protocol: process(current_state, asi_in_valid, aso_out_ready)
	begin
		case current_state is

			when wait_input =>
				int_asi_in_ready <= '1';
				aso_out_valid <= '0';
				
				if (asi_in_valid = '1') then
					next_state <= process_input;
				else
					next_state <= wait_input;
				end if;

			when process_input =>
				int_asi_in_ready <= '1';
				aso_out_valid <= '0';
				
				if (asi_in_valid = '1') then
					next_state <= full_process;
				else
					next_state <= wait_output;
				end if;
				
			when wait_output =>
				int_asi_in_ready <= '1';
				aso_out_valid <= '1';

				if (aso_out_ready = '1') then
					if (asi_in_valid = '1') then
						next_state <= process_input;
					else
						next_state <= wait_input;
					end if;
				else
			      if	(asi_in_valid = '1') then
						next_state <= wait_output_and_process;
					else
						next_state <= wait_output;
					end if;
				end if;
			
			when full_process =>
				aso_out_valid <= '1';
				int_asi_in_ready <= '1';

				if (aso_out_ready = '1' and asi_in_valid = '1') then
					next_state <= full_process;
				elsif (aso_out_ready = '1' and asi_in_valid = '0') then
					next_state <= wait_output;
				else
					int_asi_in_ready <= '0';
					next_state <= wait_output_and_process;
				end if;
				
			when wait_output_and_process =>
				int_asi_in_ready <= '0';
				aso_out_valid <= '1';

				if (aso_out_ready = '1') then
					if (asi_in_valid = '1') then
						int_asi_in_ready <= '1';
						next_state <= full_process;
					else
						next_state <= wait_output;
					end if;
				else
					next_state <= wait_output_and_process;
				end if;

				
		end case;
				
	
	end process;
	
	asi_in_ready <= int_asi_in_ready;
	
	aso_out_eop <= '0';
	aso_out_sop <= '0';
	aso_out_empty <= '0';

	avs_params_waitrequest <= '0';
end architecture rtl; -- of acc_linear_function
