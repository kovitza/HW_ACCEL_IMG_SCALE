--------------------------------------------------------------------------------
--! @file rom.vhd
--! @brief ROM memory example
--! @details This code implements simple ROM memory
--! @date 02.11.2017.
--! @version 1.0
--------------------------------------------------------------------------------
library ieee;

use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

--! @brief Entity declaration for ROM memory
--! @details
--! Simple ROM memory where data is read when rd signal is asserted.
entity rom is
	generic (
		DATA_WIDTH : integer := 8;		--! number of data lines
		ADDR_WIDTH : integer := 4		--! number of address lines
	);
	port (
		clk : in std_logic;				--! clock
		rd : in std_logic;				--! read enable
		rd_addr : in std_logic_vector(ADDR_WIDTH-1 downto 0);	--! read address
		data_out : out std_logic_vector(DATA_WIDTH-1 downto 0)--! output data
	);
end rom;

--! @brief RTL architecture for RAM
--! @details
--! Data is read from data_out when rd is asserted. Read latency is 1 clk
architecture rom_behav of rom is

type rom_t is array (0 to 2**ADDR_WIDTH-1) of std_logic_vector(DATA_WIDTH-1 downto 0);

--! initialize memory as constant
constant rom_mem : rom_t := (
	0 => x"a0",
	1 => x"a1",
	2 => x"a2",
	3 => x"a3",
	4 => x"a4",
	5 => x"a5",
	6 => x"a6",
	7 => x"a7",
	8 => x"a8",
	9 => x"a9",
	10 => x"aa",
	11 => x"ab",
	12 => x"ac",
	13 => x"ad",
	14 => x"ae",
	15 => x"af",
	others => x"00"
);

begin

--! ROM read process
rom_read: process (clk)
begin
	if (rising_edge(clk)) then
		if (rd = '1') then
			data_out <= rom_mem(to_integer(unsigned(rd_addr)));
		end if;
	end if;
end process;

end rom_behav;