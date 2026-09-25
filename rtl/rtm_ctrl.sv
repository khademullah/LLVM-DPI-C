// Single-port racetrack controller.
//
// Firmware view: one MMIO command (domain, write, data) in flight.
// The FSM shifts the head one domain per cycle, then reads or writes.
// A DPI-C scoreboard (src/dpi_bridge.c) runs the same command on the C model.

module rtm_ctrl #(
    parameter int DOMAINS = 64
) (
    input  logic        clk,
    input  logic        rst_n,
    input  logic        cmd_valid,
    input  logic        cmd_write,
    input  logic [15:0] cmd_domain,
    input  logic [31:0] cmd_wdata,
    output logic        cmd_ready,
    output logic        done,
    output logic [31:0] rdata,
    output logic [15:0] head,
    output logic        fault
);
    localparam int AW = (DOMAINS <= 2) ? 1 : $clog2(DOMAINS);

    typedef enum logic [1:0] {
        ST_IDLE,
        ST_SHIFT,
        ST_ACCESS
    } state_e;

    state_e state;
    logic [15:0] remain;
    logic        dir;
    logic        do_write;
    logic [31:0] wdata_r;
    logic [31:0] mem[0:DOMAINS-1];

    assign cmd_ready = (state == ST_IDLE);

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state    <= ST_IDLE;
            head     <= '0;
            remain   <= '0;
            dir      <= 1'b0;
            do_write <= 1'b0;
            wdata_r  <= '0;
            rdata    <= '0;
            done     <= 1'b0;
            fault    <= 1'b0;
            for (int i = 0; i < DOMAINS; i++)
                mem[i] <= '0;
        end else begin
            done <= 1'b0;
            case (state)
                ST_IDLE: begin
                    if (cmd_valid) begin
                        if (cmd_domain >= 16'(DOMAINS)) begin
                            fault <= 1'b1;
                            done  <= 1'b1;
                        end else if (cmd_domain == head) begin
                            do_write <= cmd_write;
                            wdata_r  <= cmd_wdata;
                            state    <= ST_ACCESS;
                        end else begin
                            do_write <= cmd_write;
                            wdata_r  <= cmd_wdata;
                            dir      <= (cmd_domain > head);
                            if (cmd_domain > head)
                                remain <= cmd_domain - head;
                            else
                                remain <= head - cmd_domain;
                            state <= ST_SHIFT;
                        end
                    end
                end
                ST_SHIFT: begin
                    if (dir)
                        head <= head + 16'd1;
                    else
                        head <= head - 16'd1;
                    if (remain == 16'd1)
                        state <= ST_ACCESS;
                    else
                        remain <= remain - 16'd1;
                end
                ST_ACCESS: begin
                    if (do_write)
                        mem[head[AW-1:0]] <= wdata_r;
                    else
                        rdata <= mem[head[AW-1:0]];
                    done  <= 1'b1;
                    state <= ST_IDLE;
                end
                default: begin
                    fault <= 1'b1;
                    state <= ST_IDLE;
                end
            endcase
        end
    end
endmodule
