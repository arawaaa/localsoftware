import { AppBar, Button, Toolbar, Typography, Container, Box, CssBaseline, ThemeProvider, createTheme } from '@mui/material';
import HeroPanel from './components/HeroPanel';
import InfoCards from './components/InfoCards';
import ProjectCards from './components/ProjectCards';
import EducationSection from './components/EducationSection';
import SkillPills from './components/SkillPills';

const theme = createTheme({
  palette: {
    mode: 'dark',
    primary: {
      main: '#90caf9', // Lighter blue for dark mode
    },
    secondary: {
      main: '#f48fb1', // Lighter pink/purple for dark mode
    },
    background: {
      default: '#0a1929', // Very dark blue/grey
      paper: '#132f4c',   // Slightly lighter for cards
    },
    text: {
      primary: '#fff',
      secondary: 'rgba(255, 255, 255, 0.7)',
    },
  },
  typography: {
    fontFamily: '"Roboto", "Helvetica", "Arial", sans-serif',
    h1: { fontWeight: 700 },
    h2: { fontWeight: 600 },
    h3: { fontWeight: 600 },
  },
  components: {
    MuiAppBar: {
      styleOverrides: {
        root: {
          backgroundColor: '#0a1929',
          backgroundImage: 'none',
        },
      },
    },
  },
});

function App() {
  return (
    <ThemeProvider theme={theme}>
      <CssBaseline />
      <Box sx={{ flexGrow: 1, m: 0, p: 0, width: '100%' }}>
        {/* Floating Header */}
        <AppBar 
          position="fixed" 
          color="primary"
          elevation={0} 
          sx={{ 
            borderBottom: '1px solid rgba(255,255,255,0.1)' 
          }}
        >
          <Toolbar>
            <Typography variant="h6" component="div" sx={{ flexGrow: 1, fontWeight: 'bold' }}>
              Arnav Rawat
            </Typography>
            <Button color="inherit" sx={{ fontWeight: 'medium' }}>
              Writing
            </Button>
          </Toolbar>
        </AppBar>
        
        {/* Main Content Area */}
        <HeroPanel />
        <InfoCards />
        <Container maxWidth="lg" sx={{ mb: 4 }}>
          <Typography>
            Apart from working on machine learning and software engineering projects, I also enjoy learning more about mathematics, including graph theory and complex analysis. Mathematics is particularly enjoyable due to its rich structure and the beauty of certainty of truth. I also enjoy learning about history, in particular early Christian religious practice following Jesus, as well as early Judaism. Outside academic interests, I engage in biking, watching sports and cooking.
            <br /> <br />
            Feel free to check out my notes, available at the link in the header.
          </Typography>
        </Container>
        <ProjectCards />
        <EducationSection />
        <SkillPills />
        
        <Box component="footer" sx={{ bgcolor: 'primary.main', color: 'white', py: 6, mt: 8 }}>
          <Container maxWidth="lg">
            <Typography variant="body2" align="center">
              © 2026 Arnav Rawat. Built with C++20, io_uring, and React.
            </Typography>
          </Container>
        </Box>
      </Box>
    </ThemeProvider>
  );
}

export default App;
