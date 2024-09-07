library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity source_stimulus is
    port (
        reset           : in std_logic;
        clk             : in std_logic;
		aso_out_data    : out std_logic_vector(7 downto 0);  -- out.data
		aso_out_ready   : in  std_logic;                     -- .ready
		aso_out_valid   : out std_logic;                     -- .valid
		aso_out_sop     : out std_logic;                     -- .startofpacket
		aso_out_eop     : out std_logic                      -- .endofpacket
    );
end entity source_stimulus;

architecture behavioral of source_stimulus is
    component rom is
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
    end component rom;
    signal index : integer range 0 to 15;
    
    signal rom_rd_addr : std_logic_vector(3 downto 0);
    signal rom_rd : std_logic;
    
    signal int_aso_out_valid : std_logic;
begin
    
    L_ROM: rom  generic map (8, 4)
                port map (clk, rom_rd, rom_rd_addr, aso_out_data);
    
    aso_out_valid <= int_aso_out_valid;
    int_aso_out_valid <= '1' when index < 12 else '0';
    aso_out_eop <= '0';
    aso_out_sop <= '0';
    rom_rd <= '1';
    rom_rd_addr <= std_logic_vector(to_unsigned(index, 4));
    
    PROC_ROM_RD_ADDR: process (reset, clk) is
    begin
        if (reset = '1') then
            index <= 0;
        elsif (rising_edge(clk)) then
            if ((int_aso_out_valid = '1') and (aso_out_ready = '1')) then
                -- pixel was sent
                if (index = 15) then
                    index <= 0;
                else
                    index <= index + 1;
                end if;
            end if;
        end if;
    end process PROC_ROM_RD_ADDR;
    
end architecture behavioral;